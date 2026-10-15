/**
	GAZLFloatLiteralTest checks that `parseFloat` rounds float literals correctly, for every positive finite float
	in a range of bit patterns (default: all of them). For each float it parses the float's exact decimal value, its
	`%.9g` form, the exact midpoint to the next float, and that midpoint nudged up and down by one digit and by a
	digit 130 places further down, and checks every result bit for bit. Every 251st float is also checked negated.

	Usage: GAZLFloatLiteralTest [first last [step]]    (bit patterns, hex or decimal, `last` exclusive)
**/

#include "GAZL.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <vector>

using namespace GAZL;

/**
	A minimal unsigned big integer, for writing the exact decimal value of `n` * 2^`k`.
**/
class Big {
	public:		explicit Big(UInt value) : words(1, value) { }
	public:		void multiplySmall(UInt factor);																/// `this` *= `factor`
	public:		std::string toDecimal() const;
	protected:	std::vector<UInt> words;																		// least significant first
};

void Big::multiplySmall(UInt factor) {
	uint64_t carry = 0;
	for (size_t i = 0; i < words.size(); ++i) {
		carry += static_cast<uint64_t>(words[i]) * factor;
		words[i] = static_cast<UInt>(carry);
		carry >>= 32;
	}
	if (carry != 0) words.push_back(static_cast<UInt>(carry));
}

std::string Big::toDecimal() const {
	std::vector<UInt> n(words);
	std::vector<UInt> chunks;																					// base 10^9, least significant first
	while (n.size() > 1 || n[0] != 0) {
		uint64_t remainder = 0;
		for (size_t i = n.size(); i-- > 0;) {
			const uint64_t x = (remainder << 32) | n[i];
			n[i] = static_cast<UInt>(x / 1000000000);
			remainder = x % 1000000000;
		}
		chunks.push_back(static_cast<UInt>(remainder));
		while (n.size() > 1 && n.back() == 0) n.pop_back();
	}
	if (chunks.empty()) return "0";
	char buffer[16];
	sprintf(buffer, "%u", chunks.back());
	std::string result(buffer);
	for (size_t i = chunks.size() - 1; i-- > 0;) {
		sprintf(buffer, "%09u", chunks[i]);
		result += buffer;
	}
	return result;
}

static std::vector<Big> gPowersOfFive;																			// 5^0 to 5^160

static std::string exactDecimal(UInt n, int k) {																// `n` * 2^`k` in plain notation
	if (k >= 0) {
		Big big(n);
		for (; k >= 16; k -= 16) big.multiplySmall(1 << 16);
		big.multiplySmall(1u << k);
		return big.toDecimal();
	}
	Big big(gPowersOfFive[-k]);
	big.multiplySmall(n);
	std::string digits = big.toDecimal();																		// n * 2^k = n * 5^-k / 10^-k
	const size_t point = static_cast<size_t>(-k);
	if (digits.size() <= point) digits = std::string(point + 1 - digits.size(), '0') + digits;
	return digits.substr(0, digits.size() - point) + "." + digits.substr(digits.size() - point);
}

static std::string scientific(const std::string& plain) {														// "d.ddde-x" form of a plain decimal
	std::string digits;
	int pointAt = static_cast<int>(plain.size());
	for (size_t i = 0; i < plain.size(); ++i) {
		if (plain[i] == '.') pointAt = static_cast<int>(i);
		else digits += plain[i];
	}
	size_t first = digits.find_first_not_of('0');
	if (first == std::string::npos) return "0.0";
	const int exponent = pointAt - static_cast<int>(first) - 1;
	std::string mantissa = digits.substr(first);
	while (mantissa.size() > 1 && mantissa[mantissa.size() - 1] == '0') mantissa.erase(mantissa.size() - 1);
	char buffer[16];
	sprintf(buffer, "e%d", exponent);
	return mantissa.substr(0, 1) + "." + (mantissa.size() > 1 ? mantissa.substr(1) : "0") + buffer;
}

static std::string decrementLastDigit(std::string s) {															// `s` minus one unit of its last digit
	size_t i = s.size();
	while (i-- > 0) {
		if (s[i] == '.') continue;
		if (s[i] != '0') { --s[i]; break; }
		s[i] = '9';
	}
	return s;
}

static int gFailures = 0;
static UInt gMaxMidpointDigits = 0;

static void check(const std::string& text, UInt expected) {
	const Char* p = text.c_str();
	const Char* const e = p + text.size();
	const Float value = parseFloat(p, e);
	UInt bits;
	memcpy(&bits, &value, sizeof (bits));
	if (p != e || bits != expected) {
		if (++gFailures <= 20) {
			printf("FAIL: %s -> 0x%08X (stopped %d from the end), expected 0x%08X\n", text.c_str(), bits
					, static_cast<int>(e - p), expected);
		}
	}
}

static UInt significantDigits(const std::string& plain) {
	UInt count = 0;
	bool started = false;
	for (size_t i = 0; i < plain.size(); ++i) {
		if (plain[i] == '.') continue;
		started = started || plain[i] != '0';
		if (started) ++count;
	}
	return count;
}

static void testFloat(UInt bits) {
	const UInt field = bits >> 23;
	const UInt mantissa = (field != 0 ? (bits & 0x7FFFFF) | 0x800000 : bits);
	const int exponent = static_cast<int>(field != 0 ? field : 1) - 150;
	const std::string value = exactDecimal(mantissa, exponent);
	check(scientific(value), bits);

	float f;
	memcpy(&f, &bits, sizeof (f));
	char buffer[64];
	sprintf(buffer, "%.9g", f);
	std::string short9(buffer);
	if (short9.find_first_of(".e") == std::string::npos) short9 += ".0";
	check(short9, bits);

	if (bits % 251 == 0) check("-" + value, bits | 0x80000000U);

	if (bits < 0x7F800000) {																					// the midpoint to the next float
		const std::string midpoint = exactDecimal(2 * mantissa + 1, exponent - 1);
		const UInt digits = significantDigits(midpoint);
		if (digits > gMaxMidpointDigits) gMaxMidpointDigits = digits;
		const bool integer = (midpoint.find('.') == std::string::npos);
		const std::string zeros(130, '0');
		const std::string nines(130, '9');
		const std::string below = decrementLastDigit(midpoint);
		check(midpoint, ((bits & 1) == 0 ? bits : bits + 1));
		check(midpoint + (integer ? ".1" : "1"), bits + 1);
		check(midpoint + (integer ? "." : "") + zeros + "1", bits + 1);
		check(below + (integer ? ".9" : "9"), bits);
		check(below + (integer ? "." : "") + nines, bits);
	}
}

int main(int argc, const char* argv[]) {
	gPowersOfFive.push_back(Big(1));
	for (int i = 1; i <= 160; ++i) {
		gPowersOfFive.push_back(gPowersOfFive.back());
		gPowersOfFive.back().multiplySmall(5);
	}
	const UInt first = (argc > 2 ? static_cast<UInt>(strtoul(argv[1], 0, 0)) : 0);
	const UInt last = (argc > 2 ? static_cast<UInt>(strtoul(argv[2], 0, 0)) : 0x7F800000);
	const UInt step = (argc > 3 ? static_cast<UInt>(strtoul(argv[3], 0, 0)) : 1);
	UInt tested = 0;
	for (UInt bits = first; bits < last; bits += step) {
		testFloat(bits);
		++tested;
		if (bits > 0xFFFFFFFF - step) break;
	}
	printf("float literals 0x%08X-0x%08X step %u: %u floats, %d failed, midpoints up to %u significant digits\n"
			, first, last, step, tested, gFailures, gMaxMidpointDigits);
	return (gFailures == 0 ? 0 : 1);
}
