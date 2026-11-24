#pragma once

#include <cstdint>
#include <functional>

#include <QString>

// 6502 disassembler covering all 256 opcodes. Unofficial opcodes use the
// mnemonics common on NESdev (SLO, LAX, DCP, KIL and so on).
namespace Disassembler
{
	enum class Mode
	{
		Implied,
		Accumulator,
		Immediate,
		ZeroPage,
		ZeroPageX,
		ZeroPageY,
		Absolute,
		AbsoluteX,
		AbsoluteY,
		Indirect,
		IndirectX,
		IndirectY,
		Relative
	};

	struct OpcodeInfo
	{
		const char *mnemonic;
		Mode mode;
		bool official;
	};

	const OpcodeInfo &opcodeInfo(uint8_t opcode);
	int operandLength(Mode mode);
	int instructionLength(uint8_t opcode);

	// Returns the byte at an address or -1 if it cannot be read
	using ReadFunction = std::function<int(uint16_t)>;

	struct Instruction
	{
		uint16_t address = 0;
		uint8_t bytes[3] = {0, 0, 0};
		int length = 1;
		// False when the opcode or an operand byte could not be read
		bool valid = false;
		const OpcodeInfo *info = nullptr;
		// Mnemonic and operand, e.g. "LDA $0200,X"
		QString text;
		// Address the operand refers to before indexing: the zero page or
		// absolute address, the pointer of an indirect mode or the branch
		// target. -1 for implied, accumulator and immediate operands.
		int operandAddress = -1;
	};

	Instruction disassemble(uint16_t address, const ReadFunction &read);
}
