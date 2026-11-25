#include <cstdio>
#include <string>

#include "NstApiDebugger.hpp"

using Nes::Api::DebuggerCondition;

namespace
{
	int failures = 0;

	int peek(void *, unsigned address)
	{
		if (address == 0x0010) return 0x34;
		if (address == 0x0011) return 0x12;
		if (address == 0xFFFF) return 0x7F;
		if (address == 0x0000) return 0x01;
		return address < 0x2000 ? 0 : -1;
	}

	const DebuggerCondition::State state = {0x8004, 0x10, 0x03, 0xFF, 0xFD, 0x24};

	bool evaluate(const char *text, long long &value)
	{
		DebuggerCondition condition;
		std::string error;
		if (!condition.Compile(text, error))
		{
			std::fprintf(stderr, "FAIL: '%s' did not compile: %s\n", text, error.c_str());
			++failures;
			return false;
		}
		value = condition.Evaluate(state, peek, nullptr);
		return true;
	}

	void expect(const char *text, long long expected)
	{
		long long value = 0;
		if (evaluate(text, value) && value != expected)
		{
			std::fprintf(stderr, "FAIL: '%s' is %lld, expected %lld\n", text, value, expected);
			++failures;
		}
	}

	void reject(const char *text, const char *fragment)
	{
		DebuggerCondition condition;
		std::string error;
		if (condition.Compile(text, error) || error.find(fragment) == std::string::npos)
		{
			std::fprintf(stderr, "FAIL: '%s' should fail with '%s', got '%s'\n", text, fragment, error.c_str());
			++failures;
		}
	}
}

int main()
{
	expect("A == $10 && X > 2", 1);
	expect("a==16 && x>3", 0);
	expect("PC == 0x8004", 1);
	expect("S == $FD || y == 0", 1);
	expect("p & $04", 4);
	expect("[$10] == $34", 1);
	expect("word[$10] == $1234", 1);
	expect("[[$0000]] == 0", 1);
	expect("[$2000] == 0", 1);
	expect("[$10000] == 0", 0);
	expect("word[$FFFF] == $7F7F", 0);
	expect("word[$FFFF]", 0x017F);
	expect("1 + 2 * 3", 7);
	expect("(1 + 2) * 3", 9);
	expect("10 - 3 - 2", 5);
	expect("1 << 4 | 1", 17);
	expect("$FF & ~$0F", 0xF0);
	expect("-1 < 0", 1);
	expect("!0 + !5", 1);
	expect("7 % 4 + 8 / 3", 5);
	expect("1 / 0 + 1 % 0", 0);
	expect("1 == 1 == 1", 1);
	expect("3 > 2 > 1", 0);
	expect("0 || 0 && 1", 0);
	expect("1 << 64", 0);
	expect("$7FFFFFFFFFFFFFFF + 1 < 0", 1);

	reject("", "empty");
	reject("   ", "empty");
	reject("A ==", "end of expression");
	reject("A = 1", "unexpected '='");
	reject("(A == 1", "expected ')'");
	reject("[$10", "expected ']'");
	reject("foo == 1", "unknown name 'foo'");
	reject("12ab", "invalid number");
	reject("$", "hexadecimal");
	reject("$FFFFFFFFFFFFFFFFF", "too large");
	reject("A == 1 2", "unexpected '2'");
	reject("A == 1 )", "unexpected ')'");
	reject(std::string(200, '(').c_str(), "nested too deeply");

	std::string longChain = "1";
	for (int index = 0; index < 600; ++index) longChain += "+1";
	reject(longChain.c_str(), "too long");

	std::string error;
	if (Nes::Api::Debugger::ValidateCondition("  ", &error) != Nes::RESULT_OK)
	{
		std::fprintf(stderr, "FAIL: blank condition is valid\n");
		++failures;
	}
	if (Nes::Api::Debugger::ValidateCondition("A ==", &error) != Nes::RESULT_ERR_INVALID_PARAM || error.empty())
	{
		std::fprintf(stderr, "FAIL: invalid condition reports an error\n");
		++failures;
	}

	DebuggerCondition keep;
	if (!keep.Compile("A == $10", error) || keep.Compile("A ==", error) || keep.Evaluate(state, peek, nullptr) != 1)
	{
		std::fprintf(stderr, "FAIL: failed compile keeps the previous expression\n");
		++failures;
	}

	return failures == 0 ? 0 : 1;
}
