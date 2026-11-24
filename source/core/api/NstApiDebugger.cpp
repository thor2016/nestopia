////////////////////////////////////////////////////////////////////////////////////////
//
// Nestopia - NES/Famicom emulator written in C++
//
// Copyright (C) 2003-2008 Martin Freij
//
// This file is part of Nestopia.
//
// Nestopia is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// Nestopia is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Nestopia; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
////////////////////////////////////////////////////////////////////////////////////////

#include <new>
#include "../NstMachine.hpp"
#include "../NstImage.hpp"
#include "NstApiDebugger.hpp"

namespace Nes
{
	namespace Api
	{
		Debugger::~Debugger()
		{
			emulator.cpu.SetDebuggerCallback( nullptr, nullptr );
		}

		void Debugger::SetCallback(const Callback function,const UserData data)
		{
			callback = function;
			userData = data;
			emulator.cpu.SetDebuggerCallback( function ? &Debugger::Dispatch : nullptr, this );
		}

		Result Debugger::SetExecBreakpoint(const uint address,const bool enabled) throw()
		{
			if (address > 0xFFFF)
				return RESULT_ERR_INVALID_PARAM;

			try
			{
				emulator.cpu.SetExecBreakpoint( address, enabled );
			}
			catch (const std::bad_alloc&)
			{
				return RESULT_ERR_OUT_OF_MEMORY;
			}

			return RESULT_OK;
		}

		void Debugger::ClearExecBreakpoints()
		{
			emulator.cpu.ClearExecBreakpoints();
		}

		void Debugger::RequestBreak()
		{
			emulator.cpu.RequestDebugBreak();
		}

		uint Debugger::GetProgramCounter() const
		{
			return emulator.cpu.GetPC() & 0xFFFF;
		}

		void Debugger::GetRegisters(Registers& registers) const
		{
			Core::Cpu::DebugRegisters regs;
			emulator.cpu.GetDebugRegisters( regs );

			registers.pc = regs.pc;
			registers.a = regs.a;
			registers.x = regs.x;
			registers.y = regs.y;
			registers.sp = regs.sp;
			registers.p = regs.p;
		}

		void Debugger::SetRegisters(const Registers& registers)
		{
			const Core::Cpu::DebugRegisters regs =
			{
				registers.pc,
				registers.a,
				registers.x,
				registers.y,
				registers.sp,
				registers.p
			};

			emulator.cpu.SetDebugRegisters( regs );
		}

		int Debugger::Peek(uint address) const
		{
			address &= 0xFFFF;

			if (address < 0x2000)
				return int(emulator.cpu.PeekDebugRam( address ));

			if (address < 0x4020)
				return -1;

			uint data;

			if (emulator.image && emulator.image->PeekDebug( address, data ))
				return int(data & 0xFF);

			return -1;
		}

		long Debugger::GetPrgRomOffset(const uint address) const
		{
			return emulator.image ? emulator.image->GetPrgRomOffset( address & 0xFFFF ) : -1;
		}

		void Debugger::GetPpuState(PpuState& state) const
		{
			Core::Ppu::DebugState ppu;
			emulator.ppu.GetDebugState( ppu );

			state.scanline = ppu.scanline;
			state.dot = ppu.dot;
			state.linesPerFrame = ppu.linesPerFrame;
			state.ctrl = ppu.ctrl;
			state.mask = ppu.mask;
			state.status = ppu.status;
			state.oamAddress = ppu.oamAddress;
			state.vramAddress = ppu.vramAddress;
			state.tempAddress = ppu.tempAddress;
			state.fineX = ppu.fineX;
			state.writeToggle = ppu.writeToggle;
		}

		qaword Debugger::GetCycles() const
		{
			return emulator.cpu.GetMonotonicCycles();
		}

		qaword Debugger::GetCpuCycles() const
		{
			return emulator.cpu.GetMonotonicCycles() / emulator.cpu.GetClock();
		}

		qaword Debugger::GetInstructionCount() const
		{
			return emulator.cpu.GetDebugInstructionCount();
		}

		void Debugger::ResetInstructionCount()
		{
			emulator.cpu.ResetDebugInstructionCount();
		}

		void NST_REGCALL Debugger::Dispatch(void* const data,const Core::Cpu::DebugEvent& event,Core::Cpu::DebugRequest& request)
		{
			Debugger& debugger = *static_cast<Debugger*>(data);

			if (!debugger.callback)
				return;

			Reason reason;

			switch (event.reason)
			{
				case Core::Cpu::DebugEvent::STEP_COMPLETE: reason = STEP_COMPLETE;   break;
				case Core::Cpu::DebugEvent::BREAK_REQUEST: reason = BREAK_REQUEST;   break;
				default:                                   reason = EXEC_BREAKPOINT; break;
			}

			const Event apiEvent = { event.address, reason, event.cycles };
			Request apiRequest = { Request::RUN, 0, 0 };

			debugger.callback( debugger.userData, apiEvent, apiRequest );

			Core::Cpu& cpu = debugger.emulator.cpu;

			request.address = apiRequest.address;
			request.cycle = 0;

			switch (apiRequest.mode)
			{
				case Request::STEP_INTO:      request.mode = Core::Cpu::DebugRequest::STEP_INTO;      break;
				case Request::STEP_OVER:      request.mode = Core::Cpu::DebugRequest::STEP_OVER;      break;
				case Request::STEP_OUT:       request.mode = Core::Cpu::DebugRequest::STEP_OUT;       break;
				case Request::RUN_TO_ADDRESS: request.mode = Core::Cpu::DebugRequest::RUN_TO_ADDRESS; break;
				case Request::RUN_TO_FRAME:   request.mode = Core::Cpu::DebugRequest::RUN_TO_FRAME;   break;

				case Request::RUN_CYCLES:

					request.mode = Core::Cpu::DebugRequest::RUN_TO_CYCLE;
					request.cycle = cpu.GetMonotonicCycles() + apiRequest.count * cpu.GetClock();
					break;

				case Request::RUN_TO_SCANLINE:

					request.mode = Core::Cpu::DebugRequest::RUN_TO_CYCLE;
					request.cycle = cpu.GetMonotonicCycles() - cpu.GetCycles() + debugger.emulator.ppu.GetDebugNextScanlineCycles();
					break;

				default:

					request.mode = Core::Cpu::DebugRequest::RUN;
					break;
			}
		}
	}
}
