////////////////////////////////////////////////////////////////////////////////////////
//
// Nestopia - NES/Famicom emulator written in C++
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

#ifndef NST_API_DEBUGGER_CONDITION_H
#define NST_API_DEBUGGER_CONDITION_H

#include <string>
#include <vector>

namespace Nes
{
	namespace Api
	{
		/**
		* Compiled breakpoint condition.
		*
		* Expressions use C operators on signed 64 bit integers: || && | ^ & == != < <= > >=
		* << >> + - * / % and the unary operators ! ~ -. A division or modulo by zero
		* yields 0. Numbers are decimal, $hex or 0xhex. The names A, X, Y, S (stack
		* pointer), P (status) and PC read the CPU registers before the instruction at
		* the breakpoint executes. [address] reads a byte and word[address] a
		* little endian 16 bit value. Reads are side effect free and give 0 where
		* the address cannot be read. Names are case insensitive.
		*/
		class DebuggerCondition
		{
		public:

			struct State
			{
				unsigned pc;
				unsigned a;
				unsigned x;
				unsigned y;
				unsigned sp;
				unsigned p;
			};

			/** Returns a byte, or a negative value when the address cannot be read. */
			typedef int (*PeekFunction)(void*,unsigned);

			/** Replaces the expression. Leaves it unchanged and fills in error on failure. */
			bool Compile(const char*,std::string& error);
			long long Evaluate(const State&,PeekFunction,void*) const;

		private:

			friend class DebuggerConditionParser;

			struct Node
			{
				enum Type
				{
					NUMBER,
					REGISTER,
					BYTE,
					WORD,
					UNARY,
					BINARY
				};

				Type type;
				int op;
				long long value;
				int lhs;
				int rhs;
			};

			long long Evaluate(int,const State&,PeekFunction,void*) const;

			std::vector<Node> nodes;
			int root = -1;
		};
	}
}

#endif
