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

#include <cctype>
#include <climits>
#include <new>
#include "NstApiDebuggerCondition.hpp"

namespace Nes
{
	namespace Api
	{
		namespace
		{
			enum Op
			{
				OP_NONE,
				OP_LOR, OP_LAND, OP_OR, OP_XOR, OP_AND,
				OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
				OP_SHL, OP_SHR, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
				OP_NOT, OP_COMPLEMENT, OP_NEGATE
			};

			enum Register
			{
				REG_PC, REG_A, REG_X, REG_Y, REG_SP, REG_P
			};

			enum { MAX_DEPTH = 64, MAX_NODES = 512 };

			int Precedence(const int op)
			{
				switch (op)
				{
					case OP_LOR:  return 1;
					case OP_LAND: return 2;
					case OP_OR:   return 3;
					case OP_XOR:  return 4;
					case OP_AND:  return 5;
					case OP_EQ:
					case OP_NE:   return 6;
					case OP_LT:
					case OP_LE:
					case OP_GT:
					case OP_GE:   return 7;
					case OP_SHL:
					case OP_SHR:  return 8;
					case OP_ADD:
					case OP_SUB:  return 9;
					case OP_MUL:
					case OP_DIV:
					case OP_MOD:  return 10;
					default:      return 0;
				}
			}

			bool IsHex(const char c)
			{
				return std::isxdigit(static_cast<unsigned char>(c)) != 0;
			}

			bool IsIdentifier(const char c)
			{
				return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
			}
		}

		class DebuggerConditionParser
		{
		public:

			DebuggerConditionParser(const char* t,std::vector<DebuggerCondition::Node>& n)
			: text(t), pos(0), depth(0), nodes(n) {}

			bool Parse(int& root,std::string& error)
			{
				try
				{
					SkipSpace();

					if (!text[pos])
						Fail("empty expression");

					root = ParseBinary( 1 );
					SkipSpace();

					if (text[pos])
						Fail( std::string("unexpected '") + text[pos] + "'" );

					return true;
				}
				catch (const Failure& failure)
				{
					error = "column " + std::to_string( failure.column ) + ": " + failure.message;
					return false;
				}
			}

		private:

			typedef DebuggerCondition::Node Node;

			struct Failure
			{
				std::string message;
				size_t column;
			};

			[[noreturn]] void Fail(const std::string& message) const
			{
				throw Failure{ message, pos + 1 };
			}

			void SkipSpace()
			{
				while (std::isspace( static_cast<unsigned char>(text[pos]) ))
					++pos;
			}

			int Add(const Node::Type type,const int op,const long long value,const int lhs,const int rhs)
			{
				if (nodes.size() >= MAX_NODES)
					Fail("expression is too long");

				const Node node = { type, op, value, lhs, rhs };
				nodes.push_back( node );
				return int(nodes.size()) - 1;
			}

			// Binary operator at the current position and its length in characters
			int PeekOperator(size_t& length) const
			{
				const char c = text[pos];
				const char n = c ? text[pos+1] : '\0';
				int op = OP_NONE;
				length = 1;

				switch (c)
				{
					case '|': if (n == '|') { op = OP_LOR; length = 2; } else op = OP_OR; break;
					case '&': if (n == '&') { op = OP_LAND; length = 2; } else op = OP_AND; break;
					case '^': op = OP_XOR; break;
					case '=': if (n == '=') { op = OP_EQ; length = 2; } break;
					case '!': if (n == '=') { op = OP_NE; length = 2; } break;
					case '<': if (n == '=') { op = OP_LE; length = 2; } else if (n == '<') { op = OP_SHL; length = 2; } else op = OP_LT; break;
					case '>': if (n == '=') { op = OP_GE; length = 2; } else if (n == '>') { op = OP_SHR; length = 2; } else op = OP_GT; break;
					case '+': op = OP_ADD; break;
					case '-': op = OP_SUB; break;
					case '*': op = OP_MUL; break;
					case '/': op = OP_DIV; break;
					case '%': op = OP_MOD; break;
				}

				return op;
			}

			int ParseBinary(const int minPrecedence)
			{
				if (++depth > MAX_DEPTH)
					Fail("expression is nested too deeply");

				int lhs = ParseUnary();

				for (;;)
				{
					SkipSpace();

					size_t length;
					const int op = PeekOperator( length );
					const int precedence = Precedence( op );

					if (precedence == 0 || precedence < minPrecedence)
						break;

					pos += length;
					const int rhs = ParseBinary( precedence + 1 );
					lhs = Add( Node::BINARY, op, 0, lhs, rhs );
				}

				--depth;
				return lhs;
			}

			int ParseUnary()
			{
				SkipSpace();

				int op = OP_NONE;

				switch (text[pos])
				{
					case '!': op = OP_NOT;        break;
					case '~': op = OP_COMPLEMENT; break;
					case '-': op = OP_NEGATE;     break;
				}

				if (op == OP_NONE || (op == OP_NOT && text[pos+1] == '='))
					return ParsePrimary();

				if (++depth > MAX_DEPTH)
					Fail("expression is nested too deeply");

				++pos;
				const int operand = ParseUnary();
				--depth;
				return Add( Node::UNARY, op, 0, operand, -1 );
			}

			int ParseMemory(const Node::Type type)
			{
				++pos;
				const int address = ParseBinary( 1 );
				SkipSpace();

				if (text[pos] != ']')
					Fail("expected ']'");

				++pos;
				return Add( type, OP_NONE, 0, address, -1 );
			}

			int ParseNumber()
			{
				unsigned long long value = 0;
				int base = 10;
				const size_t start = pos;

				if (text[pos] == '$')
				{
					base = 16;
					++pos;
				}
				else if (text[pos] == '0' && (text[pos+1] == 'x' || text[pos+1] == 'X'))
				{
					base = 16;
					pos += 2;
				}

				const size_t digits = pos;

				while (base == 16 ? IsHex( text[pos] ) : std::isdigit( static_cast<unsigned char>(text[pos]) ))
				{
					const char c = text[pos];
					const unsigned digit = std::isdigit( static_cast<unsigned char>(c) ) ? unsigned(c - '0') : unsigned(std::tolower( c ) - 'a' + 10);

					if (value > (static_cast<unsigned long long>(LLONG_MAX) - digit) / base)
					{
						pos = start;
						Fail("number is too large");
					}

					value = value * base + digit;
					++pos;
				}

				if (pos == digits)
					Fail("expected a hexadecimal number");

				if (IsIdentifier( text[pos] ))
					Fail("invalid number");

				return Add( Node::NUMBER, OP_NONE, static_cast<long long>(value), -1, -1 );
			}

			int ParsePrimary()
			{
				SkipSpace();
				const char c = text[pos];

				if (c == '(')
				{
					++pos;
					const int inner = ParseBinary( 1 );
					SkipSpace();

					if (text[pos] != ')')
						Fail("expected ')'");

					++pos;
					return inner;
				}

				if (c == '[')
					return ParseMemory( Node::BYTE );

				if (c == '$' || std::isdigit( static_cast<unsigned char>(c) ))
					return ParseNumber();

				if (std::isalpha( static_cast<unsigned char>(c) ) || c == '_')
				{
					const size_t start = pos;

					while (IsIdentifier( text[pos] ))
						++pos;

					std::string name( text + start, pos - start );

					for (char& ch : name)
						ch = char(std::tolower( static_cast<unsigned char>(ch) ));

					int reg = -1;

					if (name == "pc")                    reg = REG_PC;
					else if (name == "a")                reg = REG_A;
					else if (name == "x")                reg = REG_X;
					else if (name == "y")                reg = REG_Y;
					else if (name == "s" || name == "sp") reg = REG_SP;
					else if (name == "p")                reg = REG_P;

					if (reg >= 0)
						return Add( Node::REGISTER, OP_NONE, reg, -1, -1 );

					if (name == "byte" || name == "word")
					{
						SkipSpace();

						if (text[pos] == '[')
							return ParseMemory( name == "word" ? Node::WORD : Node::BYTE );
					}

					pos = start;
					Fail("unknown name '" + std::string( text + start, name.size() ) + "'");
				}

				if (!c)
					Fail("unexpected end of expression");

				Fail( std::string("unexpected '") + c + "'" );
			}

			const char* const text;
			size_t pos;
			int depth;
			std::vector<Node>& nodes;
		};

		bool DebuggerCondition::Compile(const char* const text,std::string& error)
		{
			std::vector<Node> compiled;
			int top = -1;

			try
			{
				DebuggerConditionParser parser( text ? text : "", compiled );

				if (!parser.Parse( top, error ))
					return false;
			}
			catch (const std::bad_alloc&)
			{
				error = "out of memory";
				return false;
			}

			nodes.swap( compiled );
			root = top;
			return true;
		}

		long long DebuggerCondition::Evaluate(const State& state,const PeekFunction peek,void* const data) const
		{
			return root < 0 ? 0 : Evaluate( root, state, peek, data );
		}

		long long DebuggerCondition::Evaluate(const int index,const State& state,const PeekFunction peek,void* const data) const
		{
			const Node& node = nodes[index];

			switch (node.type)
			{
				case Node::NUMBER:

					return node.value;

				case Node::REGISTER:

					switch (node.value)
					{
						case REG_PC: return state.pc;
						case REG_A:  return state.a;
						case REG_X:  return state.x;
						case REG_Y:  return state.y;
						case REG_SP: return state.sp;
						default:     return state.p;
					}

				case Node::BYTE:
				case Node::WORD:
				{
					const unsigned address = unsigned(Evaluate( node.lhs, state, peek, data ));
					const int lo = peek( data, address & 0xFFFF );

					if (node.type == Node::BYTE)
						return lo < 0 ? 0 : lo;

					const int hi = peek( data, (address + 1) & 0xFFFF );
					return (lo < 0 ? 0 : lo) | ((hi < 0 ? 0 : hi) << 8);
				}

				case Node::UNARY:
				{
					const long long value = Evaluate( node.lhs, state, peek, data );

					switch (node.op)
					{
						case OP_NOT:        return !value;
						case OP_COMPLEMENT: return ~value;
						default:            return static_cast<long long>(0ULL - static_cast<unsigned long long>(value));
					}
				}

				default:
					break;
			}

			// Both sides of && and || are always evaluated, reads have no side effects
			const long long l = Evaluate( node.lhs, state, peek, data );
			const long long r = Evaluate( node.rhs, state, peek, data );
			const unsigned long long ul = static_cast<unsigned long long>(l);
			const unsigned long long ur = static_cast<unsigned long long>(r);

			switch (node.op)
			{
				case OP_LOR:  return l || r;
				case OP_LAND: return l && r;
				case OP_OR:   return l | r;
				case OP_XOR:  return l ^ r;
				case OP_AND:  return l & r;
				case OP_EQ:   return l == r;
				case OP_NE:   return l != r;
				case OP_LT:   return l < r;
				case OP_LE:   return l <= r;
				case OP_GT:   return l > r;
				case OP_GE:   return l >= r;
				case OP_SHL:  return r < 0 || r > 62 ? 0 : static_cast<long long>(ul << r);
				case OP_SHR:  return r < 0 || r > 62 ? (l < 0 ? -1 : 0) : (l >> r);
				case OP_ADD:  return static_cast<long long>(ul + ur);
				case OP_SUB:  return static_cast<long long>(ul - ur);
				case OP_MUL:  return static_cast<long long>(ul * ur);
				case OP_DIV:  return r == 0 ? 0 : r == -1 ? static_cast<long long>(0ULL - ul) : l / r;
				case OP_MOD:  return r == 0 || r == -1 ? 0 : l % r;
			}

			return 0;
		}
	}
}
