#include <cstdio>
#include <initializer_list>

#include "debugger/Disassembler.h"

namespace
{
	int failures = 0;

	// Disassembles bytes placed at address, other addresses read as unmapped
	Disassembler::Instruction disassembleBytes(uint16_t address, std::initializer_list<int> bytes)
	{
		return Disassembler::disassemble(address, [address, bytes](uint16_t at) {
			const uint16_t offset = uint16_t(at - address);
			return offset < bytes.size() ? bytes.begin()[offset] : -1;
		});
	}

	void expect(uint16_t address, std::initializer_list<int> bytes, const char *text, int operandAddress)
	{
		const Disassembler::Instruction instruction = disassembleBytes(address, bytes);
		const QString expected = QString::fromLatin1(text);

		if (!instruction.valid || instruction.length != int(bytes.size()) || instruction.text != expected ||
			instruction.operandAddress != operandAddress)
		{
			std::fprintf(stderr, "FAIL: $%04X expected \"%s\" operand %d, got \"%s\" length %d operand %d valid %d\n",
				address, text, operandAddress, qPrintable(instruction.text), instruction.length,
				instruction.operandAddress, int(instruction.valid));
			++failures;
		}
	}

	void check(bool condition, const char *what)
	{
		if (!condition)
		{
			std::fprintf(stderr, "FAIL: %s\n", what);
			++failures;
		}
	}
}

int main(void)
{
	expect(0x8000, {0xEA}, "NOP", -1);
	expect(0x8000, {0x0A}, "ASL A", -1);
	expect(0x8000, {0xA9, 0x05}, "LDA #$05", -1);
	expect(0x8000, {0xA5, 0x10}, "LDA $10", 0x10);
	expect(0x8000, {0xB5, 0x10}, "LDA $10,X", 0x10);
	expect(0x8000, {0xB6, 0x10}, "LDX $10,Y", 0x10);
	expect(0x8000, {0xAD, 0x00, 0x02}, "LDA $0200", 0x0200);
	expect(0x8000, {0xBD, 0x00, 0x02}, "LDA $0200,X", 0x0200);
	expect(0x8000, {0xB9, 0x00, 0x02}, "LDA $0200,Y", 0x0200);
	expect(0x8000, {0x6C, 0xFC, 0xFF}, "JMP ($FFFC)", 0xFFFC);
	expect(0x8000, {0xA1, 0x20}, "LDA ($20,X)", 0x20);
	expect(0x8000, {0xB1, 0x20}, "LDA ($20),Y", 0x20);
	expect(0x8000, {0x20, 0x10, 0x80}, "JSR $8010", 0x8010);

	// Branch targets are relative to the following instruction and wrap
	expect(0x8000, {0xD0, 0xFE}, "BNE $8000", 0x8000);
	expect(0x8000, {0x10, 0x10}, "BPL $8012", 0x8012);
	expect(0xFFF0, {0xF0, 0x20}, "BEQ $0012", 0x0012);

	// Unofficial opcodes
	expect(0x8000, {0xA7, 0x10}, "LAX $10", 0x10);
	check(!Disassembler::opcodeInfo(0xA7).official, "LAX is unofficial");
	check(!Disassembler::opcodeInfo(0x1A).official, "NOP $1A is unofficial");
	check(!Disassembler::opcodeInfo(0xEB).official, "SBC $EB is unofficial");
	check(Disassembler::opcodeInfo(0xEA).official, "NOP $EA is official");
	check(Disassembler::opcodeInfo(0xE9).official, "SBC $E9 is official");

	int official = 0;

	for (int opcode = 0; opcode < 0x100; ++opcode)
	{
		const Disassembler::OpcodeInfo &info = Disassembler::opcodeInfo(uint8_t(opcode));
		official += info.official ? 1 : 0;
		check(Disassembler::instructionLength(uint8_t(opcode)) == 1 + Disassembler::operandLength(info.mode),
			"instruction length matches the addressing mode");
	}

	check(official == 151, "151 official opcodes");

	// An operand that cannot be read leaves the instruction invalid
	const Disassembler::Instruction truncated = disassembleBytes(0x8000, {0xAD, 0x00});
	check(!truncated.valid && truncated.length == 2, "unreadable operand byte");

	const Disassembler::Instruction unmapped = disassembleBytes(0x8000, {});
	check(!unmapped.valid && unmapped.length == 1, "unreadable opcode");

	if (failures != 0)
	{
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}

	return 0;
}
