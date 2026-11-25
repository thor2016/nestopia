#include <chrono>
#include <cstdio>
#include <thread>

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>

#include "core/EmulationThread.h"
#include "core/NesCore.h"
#include "debugger/Disassembler.h"

using Debugger = Nes::Api::Debugger;
using Request = Debugger::Request;

namespace
{
	int failures = 0;

	void check(bool condition, const char *what)
	{
		if (!condition)
		{
			std::fprintf(stderr, "FAIL: %s\n", what);
			++failures;
		}
	}

	// 32k NROM image running a loop with nested subroutine calls:
	//
	//   $8000  SEI
	//   $8001  LDX #$FF
	//   $8003  TXS
	//   $8004  JSR $8010
	//   $8007  NOP
	//   $8008  INC $10
	//   $800A  JMP $8004
	//   $8010  LDA #$01
	//   $8012  JSR $8020
	//   $8015  RTS
	//   $8020  NOP
	//   $8021  RTS
	//   $8030  RTI         NMI and IRQ handler
	QByteArray makeTestRom(void)
	{
		QByteArray rom(16 + 0x8000 + 0x2000, char(0));
		const char header[] = {'N', 'E', 'S', 0x1A, 2, 1};
		std::copy(header, header + sizeof(header), rom.begin());

		char *prg = rom.data() + 16;
		std::fill(prg, prg + 0x8000, char(0xEA));

		const unsigned char main[] = {0x78, 0xA2, 0xFF, 0x9A, 0x20, 0x10, 0x80, 0xEA, 0xE6, 0x10, 0x4C, 0x04, 0x80};
		const unsigned char sub[] = {0xA9, 0x01, 0x20, 0x20, 0x80, 0x60};
		const unsigned char sub2[] = {0xEA, 0x60};
		const unsigned char vectors[] = {0x30, 0x80, 0x00, 0x80, 0x30, 0x80};

		std::copy(main, main + sizeof(main), prg + 0x0000);
		std::copy(sub, sub + sizeof(sub), prg + 0x0010);
		std::copy(sub2, sub2 + sizeof(sub2), prg + 0x0020);
		prg[0x0030] = char(0x40);
		std::copy(vectors, vectors + sizeof(vectors), prg + 0x7FFA);

		return rom;
	}

	struct HaltLog
	{
		QMutex mutex;
		QWaitCondition changed;
		int count = 0;
		quint16 address = 0;
		int reason = -1;
	};

	int haltCount(HaltLog &log)
	{
		QMutexLocker lock(&log.mutex);
		return log.count;
	}

	bool waitForHalt(HaltLog &log, int count, quint16 &address, int &reason)
	{
		QMutexLocker lock(&log.mutex);
		QElapsedTimer timer;
		timer.start();

		while (log.count < count)
		{
			const int remaining = 5000 - int(timer.elapsed());

			if (remaining <= 0 || !log.changed.wait(&log.mutex, remaining))
			{
				return false;
			}
		}

		address = log.address;
		reason = log.reason;
		return true;
	}

	// Resumes with the request and waits for the next halt
	bool resumeAndWait(EmulationThread &thread, HaltLog &log, const Request &request, quint16 &address, int &reason)
	{
		const int next = haltCount(log) + 1;
		return thread.resume(request) && waitForHalt(log, next, address, reason);
	}

	Request makeRequest(Request::Mode mode, unsigned int address = 0, Nes::qaword count = 0)
	{
		return Request{mode, address, count};
	}

	Debugger::Registers registers(EmulationThread &thread)
	{
		EmulationThread::Locker lock(thread.mutex());
		Debugger::Registers regs;
		thread.core().debugger().GetRegisters(regs);
		return regs;
	}

	Debugger::PpuState ppuState(EmulationThread &thread)
	{
		EmulationThread::Locker lock(thread.mutex());
		Debugger::PpuState state;
		thread.core().debugger().GetPpuState(state);
		return state;
	}

	void testRunControl(void)
	{
		EmulationThread thread;
		HaltLog log;
		QString error;

		QObject::connect(&thread, &EmulationThread::halted, &thread,
			[&log](quint16 address, int reason) {
				QMutexLocker lock(&log.mutex);
				++log.count;
				log.address = address;
				log.reason = reason;
				log.changed.wakeAll();
			},
			Qt::DirectConnection);

		{
			EmulationThread::Locker lock(thread.mutex());

			if (!thread.core().loadRomData(makeTestRom(), QStringLiteral("debugger-test"), error))
			{
				std::fprintf(stderr, "FAIL: could not load the test program: %s\n", qPrintable(error));
				++failures;
				return;
			}

			Debugger &debugger = thread.core().debugger();
			check(debugger.SetExecBreakpoint(0x8004) == Nes::RESULT_OK, "set execute breakpoint");
			check(debugger.SetExecBreakpoint(0x10000) == Nes::RESULT_ERR_INVALID_PARAM, "reject out of range breakpoint");
		}

		thread.setDebuggerEnabled(true);
		thread.start();

		quint16 pc = 0;
		int reason = -1;

		if (!waitForHalt(log, 1, pc, reason))
		{
			check(false, "execute breakpoint halts the thread");
			thread.stop();
			return;
		}

		check(thread.isHalted(), "thread reports halted");
		check(pc == 0x8004 && reason == Debugger::EXEC_BREAKPOINT, "breakpoint address and reason");

		int counter;

		{
			EmulationThread::Locker lock(thread.mutex());
			Debugger &debugger = thread.core().debugger();

			check(debugger.Peek(0x8004) == 0x20, "peek PRG ROM");
			check(debugger.Peek(0x2002) == -1, "peek of a PPU register is refused");
			check(debugger.Peek(0x4016) == -1, "peek of an input register is refused");
			check(debugger.GetPrgRomOffset(0x8004) == 4, "PRG ROM offset");
			check(debugger.GetPrgRomOffset(0x0004) == -1, "no PRG ROM offset for RAM");

			counter = debugger.Peek(0x0010);
			check(counter >= 0, "peek RAM");

			const auto read = [&debugger](uint16_t address) { return debugger.Peek(address); };
			const Disassembler::Instruction jsr = Disassembler::disassemble(0x8004, read);
			check(jsr.valid && jsr.length == 3 && jsr.text == QStringLiteral("JSR $8010") && jsr.operandAddress == 0x8010,
				"disassemble JSR");
			const Disassembler::Instruction io = Disassembler::disassemble(0x2000, read);
			check(!io.valid && io.text == QStringLiteral("???"), "disassembly of an unreadable address");
		}

		check(registers(thread).sp == 0xFF, "stack pointer at the breakpoint");

		check(resumeAndWait(thread, log, makeRequest(Request::STEP_OVER), pc, reason) &&
			pc == 0x8007 && reason == Debugger::STEP_COMPLETE && registers(thread).sp == 0xFF,
			"step over a JSR stops at the return address");

		check(resumeAndWait(thread, log, makeRequest(Request::RUN), pc, reason) &&
			pc == 0x8004 && reason == Debugger::EXEC_BREAKPOINT, "run to the breakpoint again");

		{
			EmulationThread::Locker lock(thread.mutex());
			check(thread.core().debugger().Peek(0x0010) == ((counter + 1) & 0xFF), "loop ran once between breakpoints");
		}

		{
			int now;
			char condition[64];
			{
				EmulationThread::Locker lock(thread.mutex());
				Debugger &debugger = thread.core().debugger();
				now = debugger.Peek(0x0010);
				check(debugger.SetExecBreakpoint(0x8004, true, "A ==") == Nes::RESULT_ERR_INVALID_PARAM,
					"reject a breakpoint with a bad condition");
				std::snprintf(condition, sizeof(condition), "[$0010] == %d", (now + 2) & 0xFF);
				check(debugger.SetExecBreakpoint(0x8004, true, condition) == Nes::RESULT_OK, "set a conditional breakpoint");
			}

			check(resumeAndWait(thread, log, makeRequest(Request::RUN), pc, reason) &&
				pc == 0x8004 && reason == Debugger::EXEC_BREAKPOINT, "conditional breakpoint halts");

			{
				EmulationThread::Locker lock(thread.mutex());
				Debugger &debugger = thread.core().debugger();
				check(debugger.Peek(0x0010) == ((now + 2) & 0xFF), "conditional breakpoint skipped the earlier hits");
				check(debugger.SetExecBreakpoint(0x8004) == Nes::RESULT_OK, "replace it with an unconditional breakpoint");
			}

			check(resumeAndWait(thread, log, makeRequest(Request::RUN), pc, reason) &&
				pc == 0x8004 && reason == Debugger::EXEC_BREAKPOINT, "unconditional breakpoint halts on every hit");
		}

		check(resumeAndWait(thread, log, makeRequest(Request::STEP_INTO), pc, reason) && pc == 0x8010,
			"step into a JSR");
		check(resumeAndWait(thread, log, makeRequest(Request::STEP_INTO), pc, reason) && pc == 0x8012,
			"step into the next instruction");
		check(resumeAndWait(thread, log, makeRequest(Request::STEP_INTO), pc, reason) && pc == 0x8020 &&
			registers(thread).sp == 0xFB, "step into a nested JSR");
		check(resumeAndWait(thread, log, makeRequest(Request::STEP_OUT), pc, reason) && pc == 0x8015 &&
			registers(thread).sp == 0xFD, "step out of the nested subroutine");
		check(resumeAndWait(thread, log, makeRequest(Request::STEP_OUT), pc, reason) && pc == 0x8007 &&
			registers(thread).sp == 0xFF, "step out of the outer subroutine");
		check(resumeAndWait(thread, log, makeRequest(Request::RUN_TO_ADDRESS, 0x800A), pc, reason) && pc == 0x800A,
			"run to an address");

		{
			EmulationThread::Locker lock(thread.mutex());
			Debugger &debugger = thread.core().debugger();
			Debugger::Registers regs;
			debugger.GetRegisters(regs);
			regs.a = 0x42;
			debugger.SetRegisters(regs);

			// A breakpoint hit cancels a step, so the timed steps below run without them
			debugger.ClearExecBreakpoints();
		}

		check(registers(thread).a == 0x42, "set a register while halted");

		for (int i = 0; i < 300; ++i)
		{
			const Debugger::PpuState before = ppuState(thread);
			const int expected = before.scanline == int(before.linesPerFrame) - 2 ? -1 : before.scanline + 1;

			check(resumeAndWait(thread, log, makeRequest(Request::RUN_TO_SCANLINE), pc, reason), "run to next scanline");

			const Debugger::PpuState after = ppuState(thread);

			if (after.scanline != expected || after.dot >= 30)
			{
				std::fprintf(stderr, "scanline %d dot %u, then scanline %d dot %u\n",
					before.scanline, before.dot, after.scanline, after.dot);
				check(false, "next scanline stops at the start of the following line");
			}
		}

		{
			quint64 framesBefore;

			{
				EmulationThread::Locker lock(thread.mutex());
				framesBefore = thread.core().frameCount();
			}

			check(resumeAndWait(thread, log, makeRequest(Request::RUN_TO_FRAME), pc, reason), "run to next frame");

			const Debugger::PpuState state = ppuState(thread);
			EmulationThread::Locker lock(thread.mutex());

			check(thread.core().frameCount() == framesBefore + 1, "next frame completes exactly one frame");
			check(state.scanline == 241 && state.dot < 30, "a frame starts at the first VBlank line");
		}

		{
			Nes::qaword before;

			{
				EmulationThread::Locker lock(thread.mutex());
				before = thread.core().debugger().GetCpuCycles();
			}

			check(resumeAndWait(thread, log, makeRequest(Request::RUN_CYCLES, 0, 100), pc, reason), "run cycles");

			EmulationThread::Locker lock(thread.mutex());
			const Nes::qaword elapsed = thread.core().debugger().GetCpuCycles() - before;
			check(elapsed >= 100 && elapsed < 107, "run cycles stops at the first instruction after the count");
		}

		check(thread.resume(makeRequest(Request::RUN)), "resume running");
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		check(!thread.isHalted(), "no halt without breakpoints");

		int next = haltCount(log) + 1;
		thread.requestBreak();
		check(waitForHalt(log, next, pc, reason) && reason == Debugger::BREAK_REQUEST, "break request halts");

		{
			// Must take the machine out of the halt before a reset
			EmulationThread::HaltGuard guard(thread);
			check(!thread.isHalted(), "halt guard leaves the halt");

			EmulationThread::Locker lock(thread.mutex());
			thread.core().reset(false);
		}

		next = haltCount(log) + 1;
		thread.requestBreak();
		check(waitForHalt(log, next, pc, reason) && reason == Debugger::BREAK_REQUEST && pc >= 0x8000,
			"halts again after a reset");

		// Stopping must not hang on a halted thread
		thread.stop();
		check(!thread.isHalted() && !thread.isRunning(), "stop while halted");
	}

	// Breaks on many instructions with a mix of run requests and inspects the
	// machine every time. None of it may change what the machine does.
	struct Perturber
	{
		Debugger *debugger = nullptr;
		unsigned long events = 0;
		unsigned long checksum = 0;
	};

	void NST_CALLBACK perturb(void *userData, const Debugger::Event &event, Request &request)
	{
		Perturber &perturber = *static_cast<Perturber *>(userData);
		Debugger &debugger = *perturber.debugger;

		Debugger::Registers regs;
		Debugger::PpuState ppu;
		debugger.GetRegisters(regs);
		debugger.GetPpuState(ppu);

		for (unsigned int address = 0; address < 0x10000; address += 0x0FFF)
		{
			perturber.checksum += unsigned(debugger.Peek(address));
		}

		perturber.checksum += unsigned(debugger.GetPrgRomOffset(event.address)) + regs.sp + unsigned(ppu.scanline);

		static const Request::Mode modes[] =
		{
			Request::STEP_INTO, Request::STEP_OVER, Request::STEP_OUT, Request::RUN_TO_SCANLINE,
			Request::RUN_CYCLES, Request::RUN, Request::RUN_TO_FRAME
		};

		request.mode = modes[perturber.events % (sizeof(modes) / sizeof(modes[0]))];
		request.count = 5;
		++perturber.events;
	}

	QByteArray runFrames(const QString &rom, const QString &database, bool debug, unsigned long &events)
	{
		NesCore core;
		QString error;

		if (!database.isEmpty())
		{
			core.loadDatabase(database);
		}

		if (!core.loadRom(rom, error))
		{
			std::fprintf(stderr, "Could not load test ROM: %s\n", qPrintable(error));
			return QByteArray();
		}

		Perturber perturber;
		perturber.debugger = &core.debugger();

		if (debug)
		{
			for (unsigned int address = 0; address < 0x10000; address += 7)
			{
				core.debugger().SetExecBreakpoint(address);
			}

			core.debugger().SetCallback(perturb, &perturber);
		}

		QCryptographicHash hash(QCryptographicHash::Sha1);

		for (int frame = 0; frame < 180; ++frame)
		{
			core.runFrame();
			hash.addData(QByteArrayView(reinterpret_cast<const char *>(core.frameBuffer()),
				qsizetype(core.frameWidth()) * core.frameHeight() * qsizetype(sizeof(uint32_t))));
		}

		for (unsigned int address = 0; address < 0x800; ++address)
		{
			const char data = char(core.peekCpu(address));
			hash.addData(QByteArrayView(&data, 1));
		}

		core.debugger().SetCallback(nullptr);
		events = perturber.events;
		return hash.result();
	}
}

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);

	testRunControl();

	if (argc > 1 && argv[1][0] != '\0')
	{
		const QString rom = QString::fromLocal8Bit(argv[1]);
		const QString database = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
		unsigned long plainEvents = 0;
		unsigned long debugEvents = 0;

		const QByteArray plain = runFrames(rom, database, false, plainEvents);
		const QByteArray debugged = runFrames(rom, database, true, debugEvents);

		check(!plain.isEmpty() && plain == debugged, "the debugger run loop and inspection do not change emulation");
		check(debugEvents > 1000, "the debugger callback ran during the comparison");
	}
	else
	{
		std::fprintf(stderr, "No test ROM given, skipping the emulation comparison\n");
	}

	if (failures != 0)
	{
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}

	return 0;
}
