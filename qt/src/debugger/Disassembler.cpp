#include "debugger/Disassembler.h"

#include <cstring>

namespace Disassembler
{
	namespace
	{
		using M = Mode;

		constexpr M IMP = M::Implied;
		constexpr M ACC = M::Accumulator;
		constexpr M IMM = M::Immediate;
		constexpr M ZP = M::ZeroPage;
		constexpr M ZPX = M::ZeroPageX;
		constexpr M ZPY = M::ZeroPageY;
		constexpr M ABS = M::Absolute;
		constexpr M ABX = M::AbsoluteX;
		constexpr M ABY = M::AbsoluteY;
		constexpr M IND = M::Indirect;
		constexpr M IZX = M::IndirectX;
		constexpr M IZY = M::IndirectY;
		constexpr M REL = M::Relative;

		struct Entry
		{
			const char *mnemonic;
			Mode mode;
		};

		const Entry entries[256] =
		{
			// 0x00
			{"BRK", IMP}, {"ORA", IZX}, {"KIL", IMP}, {"SLO", IZX}, {"NOP", ZP }, {"ORA", ZP }, {"ASL", ZP }, {"SLO", ZP },
			{"PHP", IMP}, {"ORA", IMM}, {"ASL", ACC}, {"ANC", IMM}, {"NOP", ABS}, {"ORA", ABS}, {"ASL", ABS}, {"SLO", ABS},
			// 0x10
			{"BPL", REL}, {"ORA", IZY}, {"KIL", IMP}, {"SLO", IZY}, {"NOP", ZPX}, {"ORA", ZPX}, {"ASL", ZPX}, {"SLO", ZPX},
			{"CLC", IMP}, {"ORA", ABY}, {"NOP", IMP}, {"SLO", ABY}, {"NOP", ABX}, {"ORA", ABX}, {"ASL", ABX}, {"SLO", ABX},
			// 0x20
			{"JSR", ABS}, {"AND", IZX}, {"KIL", IMP}, {"RLA", IZX}, {"BIT", ZP }, {"AND", ZP }, {"ROL", ZP }, {"RLA", ZP },
			{"PLP", IMP}, {"AND", IMM}, {"ROL", ACC}, {"ANC", IMM}, {"BIT", ABS}, {"AND", ABS}, {"ROL", ABS}, {"RLA", ABS},
			// 0x30
			{"BMI", REL}, {"AND", IZY}, {"KIL", IMP}, {"RLA", IZY}, {"NOP", ZPX}, {"AND", ZPX}, {"ROL", ZPX}, {"RLA", ZPX},
			{"SEC", IMP}, {"AND", ABY}, {"NOP", IMP}, {"RLA", ABY}, {"NOP", ABX}, {"AND", ABX}, {"ROL", ABX}, {"RLA", ABX},
			// 0x40
			{"RTI", IMP}, {"EOR", IZX}, {"KIL", IMP}, {"SRE", IZX}, {"NOP", ZP }, {"EOR", ZP }, {"LSR", ZP }, {"SRE", ZP },
			{"PHA", IMP}, {"EOR", IMM}, {"LSR", ACC}, {"ALR", IMM}, {"JMP", ABS}, {"EOR", ABS}, {"LSR", ABS}, {"SRE", ABS},
			// 0x50
			{"BVC", REL}, {"EOR", IZY}, {"KIL", IMP}, {"SRE", IZY}, {"NOP", ZPX}, {"EOR", ZPX}, {"LSR", ZPX}, {"SRE", ZPX},
			{"CLI", IMP}, {"EOR", ABY}, {"NOP", IMP}, {"SRE", ABY}, {"NOP", ABX}, {"EOR", ABX}, {"LSR", ABX}, {"SRE", ABX},
			// 0x60
			{"RTS", IMP}, {"ADC", IZX}, {"KIL", IMP}, {"RRA", IZX}, {"NOP", ZP }, {"ADC", ZP }, {"ROR", ZP }, {"RRA", ZP },
			{"PLA", IMP}, {"ADC", IMM}, {"ROR", ACC}, {"ARR", IMM}, {"JMP", IND}, {"ADC", ABS}, {"ROR", ABS}, {"RRA", ABS},
			// 0x70
			{"BVS", REL}, {"ADC", IZY}, {"KIL", IMP}, {"RRA", IZY}, {"NOP", ZPX}, {"ADC", ZPX}, {"ROR", ZPX}, {"RRA", ZPX},
			{"SEI", IMP}, {"ADC", ABY}, {"NOP", IMP}, {"RRA", ABY}, {"NOP", ABX}, {"ADC", ABX}, {"ROR", ABX}, {"RRA", ABX},
			// 0x80
			{"NOP", IMM}, {"STA", IZX}, {"NOP", IMM}, {"SAX", IZX}, {"STY", ZP }, {"STA", ZP }, {"STX", ZP }, {"SAX", ZP },
			{"DEY", IMP}, {"NOP", IMM}, {"TXA", IMP}, {"XAA", IMM}, {"STY", ABS}, {"STA", ABS}, {"STX", ABS}, {"SAX", ABS},
			// 0x90
			{"BCC", REL}, {"STA", IZY}, {"KIL", IMP}, {"AHX", IZY}, {"STY", ZPX}, {"STA", ZPX}, {"STX", ZPY}, {"SAX", ZPY},
			{"TYA", IMP}, {"STA", ABY}, {"TXS", IMP}, {"TAS", ABY}, {"SHY", ABX}, {"STA", ABX}, {"SHX", ABY}, {"AHX", ABY},
			// 0xA0
			{"LDY", IMM}, {"LDA", IZX}, {"LDX", IMM}, {"LAX", IZX}, {"LDY", ZP }, {"LDA", ZP }, {"LDX", ZP }, {"LAX", ZP },
			{"TAY", IMP}, {"LDA", IMM}, {"TAX", IMP}, {"LAX", IMM}, {"LDY", ABS}, {"LDA", ABS}, {"LDX", ABS}, {"LAX", ABS},
			// 0xB0
			{"BCS", REL}, {"LDA", IZY}, {"KIL", IMP}, {"LAX", IZY}, {"LDY", ZPX}, {"LDA", ZPX}, {"LDX", ZPY}, {"LAX", ZPY},
			{"CLV", IMP}, {"LDA", ABY}, {"TSX", IMP}, {"LAS", ABY}, {"LDY", ABX}, {"LDA", ABX}, {"LDX", ABY}, {"LAX", ABY},
			// 0xC0
			{"CPY", IMM}, {"CMP", IZX}, {"NOP", IMM}, {"DCP", IZX}, {"CPY", ZP }, {"CMP", ZP }, {"DEC", ZP }, {"DCP", ZP },
			{"INY", IMP}, {"CMP", IMM}, {"DEX", IMP}, {"AXS", IMM}, {"CPY", ABS}, {"CMP", ABS}, {"DEC", ABS}, {"DCP", ABS},
			// 0xD0
			{"BNE", REL}, {"CMP", IZY}, {"KIL", IMP}, {"DCP", IZY}, {"NOP", ZPX}, {"CMP", ZPX}, {"DEC", ZPX}, {"DCP", ZPX},
			{"CLD", IMP}, {"CMP", ABY}, {"NOP", IMP}, {"DCP", ABY}, {"NOP", ABX}, {"CMP", ABX}, {"DEC", ABX}, {"DCP", ABX},
			// 0xE0
			{"CPX", IMM}, {"SBC", IZX}, {"NOP", IMM}, {"ISC", IZX}, {"CPX", ZP }, {"SBC", ZP }, {"INC", ZP }, {"ISC", ZP },
			{"INX", IMP}, {"SBC", IMM}, {"NOP", IMP}, {"SBC", IMM}, {"CPX", ABS}, {"SBC", ABS}, {"INC", ABS}, {"ISC", ABS},
			// 0xF0
			{"BEQ", REL}, {"SBC", IZY}, {"KIL", IMP}, {"ISC", IZY}, {"NOP", ZPX}, {"SBC", ZPX}, {"INC", ZPX}, {"ISC", ZPX},
			{"SED", IMP}, {"SBC", ABY}, {"NOP", IMP}, {"ISC", ABY}, {"NOP", ABX}, {"SBC", ABX}, {"INC", ABX}, {"ISC", ABX}
		};

		bool isOfficial(uint8_t opcode, const char *mnemonic)
		{
			static const char *const unofficial[] =
			{
				"KIL", "SLO", "RLA", "SRE", "RRA", "SAX", "LAX", "DCP", "ISC", "ANC",
				"ALR", "ARR", "XAA", "AXS", "AHX", "SHY", "SHX", "TAS", "LAS"
			};

			if (opcode == 0xEB)
			{
				return false;
			}

			if (std::strcmp(mnemonic, "NOP") == 0)
			{
				return opcode == 0xEA;
			}

			for (const char *name : unofficial)
			{
				if (std::strcmp(mnemonic, name) == 0)
				{
					return false;
				}
			}

			return true;
		}

		struct Table
		{
			OpcodeInfo info[256];

			Table(void)
			{
				for (int i = 0; i < 256; ++i)
				{
					info[i] = {entries[i].mnemonic, entries[i].mode, isOfficial(uint8_t(i), entries[i].mnemonic)};
				}
			}
		};

		const Table &table(void)
		{
			static const Table instance;
			return instance;
		}

		QString hex2(unsigned value)
		{
			return QStringLiteral("$%1").arg(value & 0xFF, 2, 16, QLatin1Char('0')).toUpper();
		}

		QString hex4(unsigned value)
		{
			return QStringLiteral("$%1").arg(value & 0xFFFF, 4, 16, QLatin1Char('0')).toUpper();
		}
	}

	const OpcodeInfo &opcodeInfo(uint8_t opcode)
	{
		return table().info[opcode];
	}

	int operandLength(Mode mode)
	{
		switch (mode)
		{
			case Mode::Implied:
			case Mode::Accumulator:
				return 0;

			case Mode::Absolute:
			case Mode::AbsoluteX:
			case Mode::AbsoluteY:
			case Mode::Indirect:
				return 2;

			default:
				return 1;
		}
	}

	int instructionLength(uint8_t opcode)
	{
		return 1 + operandLength(opcodeInfo(opcode).mode);
	}

	Instruction disassemble(uint16_t address, const ReadFunction &read)
	{
		Instruction instruction;
		instruction.address = address;

		const int opcode = read(address);

		if (opcode < 0)
		{
			instruction.text = QStringLiteral("???");
			return instruction;
		}

		const OpcodeInfo &info = opcodeInfo(uint8_t(opcode));
		instruction.info = &info;
		instruction.bytes[0] = uint8_t(opcode);
		instruction.length = 1 + operandLength(info.mode);

		for (int i = 1; i < instruction.length; ++i)
		{
			const int data = read(uint16_t(address + i));

			if (data < 0)
			{
				instruction.length = i;
				instruction.text = QString::fromLatin1(info.mnemonic) + QStringLiteral(" ???");
				return instruction;
			}

			instruction.bytes[i] = uint8_t(data);
		}

		instruction.valid = true;

		const unsigned byte = instruction.bytes[1];
		const unsigned word = instruction.bytes[1] | unsigned(instruction.bytes[2]) << 8;
		QString operand;

		switch (info.mode)
		{
			case Mode::Implied:
				break;

			case Mode::Accumulator:
				operand = QStringLiteral("A");
				break;

			case Mode::Immediate:
				operand = QLatin1Char('#') + hex2(byte);
				break;

			case Mode::ZeroPage:
				operand = hex2(byte);
				instruction.operandAddress = int(byte);
				break;

			case Mode::ZeroPageX:
				operand = hex2(byte) + QStringLiteral(",X");
				instruction.operandAddress = int(byte);
				break;

			case Mode::ZeroPageY:
				operand = hex2(byte) + QStringLiteral(",Y");
				instruction.operandAddress = int(byte);
				break;

			case Mode::Absolute:
				operand = hex4(word);
				instruction.operandAddress = int(word);
				break;

			case Mode::AbsoluteX:
				operand = hex4(word) + QStringLiteral(",X");
				instruction.operandAddress = int(word);
				break;

			case Mode::AbsoluteY:
				operand = hex4(word) + QStringLiteral(",Y");
				instruction.operandAddress = int(word);
				break;

			case Mode::Indirect:
				operand = QLatin1Char('(') + hex4(word) + QLatin1Char(')');
				instruction.operandAddress = int(word);
				break;

			case Mode::IndirectX:
				operand = QLatin1Char('(') + hex2(byte) + QStringLiteral(",X)");
				instruction.operandAddress = int(byte);
				break;

			case Mode::IndirectY:
				operand = QLatin1Char('(') + hex2(byte) + QStringLiteral("),Y");
				instruction.operandAddress = int(byte);
				break;

			case Mode::Relative:
			{
				const uint16_t target = uint16_t(address + 2 + int8_t(byte));
				operand = hex4(target);
				instruction.operandAddress = int(target);
				break;
			}
		}

		instruction.text = QString::fromLatin1(info.mnemonic);

		if (!operand.isEmpty())
		{
			instruction.text += QLatin1Char(' ') + operand;
		}

		return instruction;
	}
}
