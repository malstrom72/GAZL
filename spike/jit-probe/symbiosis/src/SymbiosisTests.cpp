#include "SymbiosisTests.h"
#include "SymbiosisCpp.h"

#include <string.h>
#include <stdio.h>
#include <algorithm>
#include <vector>

namespace symbiosis {

namespace {

UTF8Z lastTestFailure[256] = { 0 };

static bool setFailure(UInt32 line, const char* expression) {
	const int written = snprintf(lastTestFailure, sizeof(lastTestFailure),
			"Symbiosis self-test failed at line %u: %s", static_cast<unsigned>(line), expression);
	if (written < 0) {
		lastTestFailure[0] = 0;
	}
	return false;
}

static bool nearEqual(Float32 a, Float32 b) {
	const Float32 d = a > b ? (a - b) : (b - a);
	return d <= 1.0e-4f;
}

static bool checkText(const UTF8Z* actual, const UTF8Z* expected) {
	return actual != 0 && expected != 0 && strcmp(actual, expected) == 0;
}

static bool checkUTF8Bytes(const UTF8Z* actual, const UByte8* expected, UInt32 size) {
	if (actual == 0 || expected == 0) {
		return false;
	}
	for (UInt32 i = 0u; i < size; ++i) {
		if (static_cast<UByte8>(actual[i]) != expected[i]) {
			return false;
		}
	}
	return true;
}

static bool checkUTF16Units(const uint16_t* actual, const uint16_t* expected, UInt32 size) {
	if (actual == 0 || expected == 0) {
		return false;
	}
	for (UInt32 i = 0u; i < size; ++i) {
		if (actual[i] != expected[i]) {
			return false;
		}
	}
	return true;
}

#define SYMBIOSIS_TEST_CHECK(expr) do { if (!(expr)) { SYMBIOSIS_ASSERT(0); return setFailure(static_cast<UInt32>(__LINE__), #expr); } } while (0)

} // namespace

const UTF8Z* getLastTestFailure() {
	return lastTestFailure;
}

struct TestItem {
	int key;
	int originalIndex;
};

static bool itemsEqual(TestItem const& left, TestItem const& right) {
	return left.key == right.key && left.originalIndex == right.originalIndex;
}

static std::vector<TestItem> makeSortTestItems(std::initializer_list<int> keys) {
	std::vector<TestItem> items;
	items.reserve((int)keys.size());

	int originalIndex = 0;
	for (int key : keys) {
		TestItem item;
		item.key = key;
		item.originalIndex = originalIndex++;
		items.push_back(item);
	}

	return items;
}

static void runSort(std::vector<TestItem>& items, int from, int to) {
	auto compare = [](TestItem const& left, TestItem const& right) -> int {
		if (left.key < right.key)
			return -1;
		if (left.key > right.key)
			return 1;
		return 0;
	};

	stableSortNoAlloc(items.data(), from, to, compare);
}

static bool requireSortedAndStableInRange(std::vector<TestItem> const& items, int from, int to) {
	for (int index = from + 1; index < to; ++index) {
		SYMBIOSIS_TEST_CHECK(items[index - 1].key <= items[index].key);

		if (items[index - 1].key == items[index].key) {
			SYMBIOSIS_TEST_CHECK(items[index - 1].originalIndex <= items[index].originalIndex);
		}
	}
	return true;
}

static bool requireEqualVectors(std::vector<TestItem> const& actual, std::vector<TestItem> const& expected) {
	SYMBIOSIS_TEST_CHECK(actual.size() == expected.size());

	for (size_t index = 0; index < actual.size(); ++index) {
		SYMBIOSIS_TEST_CHECK(itemsEqual(actual[index], expected[index]));
	}

	return true;
}

static bool runSortingCase(std::vector<TestItem> const& input, int from, int to) {
	std::vector<TestItem> actual = input;
	std::vector<TestItem> expected = input;

	runSort(actual, from, to);

	std::stable_sort(expected.begin() + from, expected.begin() + to, [](TestItem const& left, TestItem const& right) {
		return left.key < right.key;
	});

	SYMBIOSIS_TEST_CHECK(requireSortedAndStableInRange(actual, from, to));
	SYMBIOSIS_TEST_CHECK(requireEqualVectors(actual, expected));
	return true;
}

static uint32_t nextRandom(uint32_t& state) {
	state = state * 1664525u + 1013904223u;
	return state;
}

static int randomInRange(uint32_t& state, int low, int high) {
	const uint32_t value = nextRandom(state);
	const uint32_t span = (uint32_t)(high - low + 1);
	return low + (int)(value % span);
}

static bool runDirectedSortingTests() {
	struct DirectedCase {
		std::vector<TestItem> items;
		int from;
		int to;
	};

	std::vector<DirectedCase> cases = {
		{ makeSortTestItems({}), 0, 0 },
		{ makeSortTestItems({5}), 0, 1 },
		{ makeSortTestItems({1, 2}), 0, 2 },
		{ makeSortTestItems({2, 1}), 0, 2 },
		{ makeSortTestItems({1, 2, 3, 4, 5, 6, 7, 8}), 0, 8 },
		{ makeSortTestItems({8, 7, 6, 5, 4, 3, 2, 1}), 0, 8 },
		{ makeSortTestItems({7, 7, 7, 7, 7, 7, 7, 7}), 0, 8 },
		{ makeSortTestItems({4, 1, 4, 3, 4, 2, 4, 1, 4, 0, 4, 3, 4, 2, 4, 1}), 0, 16 },
		{ makeSortTestItems({0, -5, 12, -1, 7, -5, 0, 9, -100, 12, 3}), 0, 11 },
		{ makeSortTestItems({9, 1, 8, 2, 7, 3, 6, 4, 5}), 0, 9 },
		{ makeSortTestItems({9, 1, 8, 2, 7, 3, 6, 4}), 0, 8 },
		{ makeSortTestItems({3, 1, 3, 1, 3, 1, 3, 1, 2, 2, 2, 2, 3, 1, 3, 1, 3, 1, 3, 1}), 0, 20 },
		{ makeSortTestItems({99, 88, 5, 4, 3, 2, 1, 77, 66}), 2, 7 }
	};

	for (int size = 0; size <= 20; ++size) {
		std::vector<TestItem> items;
		items.reserve(size);

		for (int index = 0; index < size; ++index) {
			TestItem item;
			item.key = (size - index) % 5;
			item.originalIndex = index;
			items.push_back(item);
		}

		DirectedCase testCase;
		testCase.items = items;
		testCase.from = 0;
		testCase.to = size;
		cases.push_back(testCase);
	}

	for (size_t index = 0; index < cases.size(); ++index)
		SYMBIOSIS_TEST_CHECK(runSortingCase(cases[index].items, cases[index].from, cases[index].to));

	std::vector<TestItem> idempotence = makeSortTestItems({4, 1, 4, 2, 4, 3, 4, 0, 4, 1, 4, 2});
	std::vector<TestItem> once = idempotence;
	std::vector<TestItem> twice = idempotence;

	runSort(once, 0, (int)once.size());
	runSort(twice, 0, (int)twice.size());
	runSort(twice, 0, (int)twice.size());

	SYMBIOSIS_TEST_CHECK(requireEqualVectors(once, twice));
	return true;
}

static bool runRandomSortingTests() {
	uint32_t state = 0x12345678u;

	for (int iteration = 0; iteration < 5000; ++iteration) {
		int size = randomInRange(state, 0, 400);
		std::vector<TestItem> items;
		items.reserve(size);

		for (int index = 0; index < size; ++index) {
			TestItem item;
			item.key = randomInRange(state, -20, 20);
			item.originalIndex = index;
			items.push_back(item);
		}

		SYMBIOSIS_TEST_CHECK(runSortingCase(items, 0, size));
	}

	for (int iteration = 0; iteration < 100; ++iteration) {
		int size = randomInRange(state, 1000, 5000);
		std::vector<TestItem> items;
		items.reserve(size);

		for (int index = 0; index < size; ++index) {
			TestItem item;
			item.key = randomInRange(state, -200, 200);
			item.originalIndex = index;
			items.push_back(item);
		}

		SYMBIOSIS_TEST_CHECK(runSortingCase(items, 0, size));
	}

	for (int iteration = 0; iteration < 2000; ++iteration) {
		int size = randomInRange(state, 0, 300);
		std::vector<TestItem> items;
		items.reserve(size);

		for (int index = 0; index < size; ++index) {
			TestItem item;
			item.key = randomInRange(state, -10, 10);
			item.originalIndex = index;
			items.push_back(item);
		}

		int from = randomInRange(state, 0, size);
		int to = randomInRange(state, from, size);
		SYMBIOSIS_TEST_CHECK(runSortingCase(items, from, to));
	}

	return true;
}

static bool runParameterTextConversionTests() {
	UTF8Z text[64];
	Float32 value = 0.0f;

	const SymbiosisParameterInfo linear = { 1, "Linear", "", 0, SYMBIOSIS_PARAMETER_SCALE_LINEAR, 0.0f, -10.0f, 10.0f, 0 };
	const SymbiosisParameterInfo custom = { 1, "Custom", "", 0, SYMBIOSIS_PARAMETER_SCALE_CUSTOM, 0.0f, 0.0f, 1.0f, 0 };

	const UTF8Z* booleanTexts[2] = { "No", "Yes" };
	const SymbiosisParameterInfo booleanDefault = { 1, "Bool", "", 0, SYMBIOSIS_PARAMETER_SCALE_BOOLEAN, 0.0f, 0.0f, 1.0f, 0 };
	const SymbiosisParameterInfo booleanCustom = { 1, "BoolCustom", "", 0, SYMBIOSIS_PARAMETER_SCALE_BOOLEAN, 0.0f, 0.0f, 1.0f, booleanTexts };

	const UTF8Z* steppedTexts[4] = { "Zero", "One", "Two", "Three" };
	const SymbiosisParameterInfo steppedDefault = { 1, "Step", "", 0, SYMBIOSIS_PARAMETER_SCALE_STEPPED, 0.0f, 0.0f, 3.0f, 0 };
	const SymbiosisParameterInfo steppedCustom = { 1, "StepCustom", "", 0, SYMBIOSIS_PARAMETER_SCALE_STEPPED, 0.0f, 0.0f, 3.0f, steppedTexts };
	const SymbiosisParameterInfo zeroRange = { 1, "ZeroRange", "", 0, SYMBIOSIS_PARAMETER_SCALE_LINEAR, 0.0f, 5.0f, 5.0f, 0 };

	convertParameterValueToTextDefault(linear, 0.25f, static_cast<UInt32>(sizeof(text) - 1u), text);
	SYMBIOSIS_TEST_CHECK(checkText(text, "-5"));
	convertParameterValueToTextDefault(linear, 0.0f, static_cast<UInt32>(sizeof(text) - 1u), text);
	SYMBIOSIS_TEST_CHECK(checkText(text, "-10"));
	convertParameterValueToTextDefault(linear, 1.0f, static_cast<UInt32>(sizeof(text) - 1u), text);
	SYMBIOSIS_TEST_CHECK(checkText(text, "10"));

	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(linear, "-5", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 0.25f));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(linear, " 10 ", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 1.0f));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(linear, "-10", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 0.0f));
	SYMBIOSIS_TEST_CHECK(!convertTextToParameterValueDefault(linear, "x", &value));

	convertParameterValueToTextDefault(custom, 0.5f, static_cast<UInt32>(sizeof(text) - 1u), text);
	SYMBIOSIS_TEST_CHECK(checkText(text, "0.5"));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(custom, "0.5", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 0.5f));

	convertParameterValueToTextDefault(booleanDefault, 0.0f, static_cast<UInt32>(sizeof(text) - 1u), text);
	SYMBIOSIS_TEST_CHECK(checkText(text, "Off"));
	convertParameterValueToTextDefault(booleanDefault, 1.0f, static_cast<UInt32>(sizeof(text) - 1u), text);
	SYMBIOSIS_TEST_CHECK(checkText(text, "On"));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(booleanDefault, "off", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 0.0f));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(booleanDefault, "YES", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 1.0f));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(booleanDefault, "1", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 1.0f));
	SYMBIOSIS_TEST_CHECK(!convertTextToParameterValueDefault(booleanDefault, "maybe", &value));

	convertParameterValueToTextDefault(booleanCustom, 0.0f, static_cast<UInt32>(sizeof(text) - 1u), text);
	SYMBIOSIS_TEST_CHECK(checkText(text, "No"));
	convertParameterValueToTextDefault(booleanCustom, 1.0f, static_cast<UInt32>(sizeof(text) - 1u), text);
	SYMBIOSIS_TEST_CHECK(checkText(text, "Yes"));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(booleanCustom, "No", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 0.0f));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(booleanCustom, "Yes", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 1.0f));

	convertParameterValueToTextDefault(steppedDefault, 0.0f, static_cast<UInt32>(sizeof(text) - 1u), text);
	SYMBIOSIS_TEST_CHECK(checkText(text, "0"));
	convertParameterValueToTextDefault(steppedDefault, 0.34f, static_cast<UInt32>(sizeof(text) - 1u), text);
	SYMBIOSIS_TEST_CHECK(checkText(text, "1"));
	convertParameterValueToTextDefault(steppedDefault, 1.0f, static_cast<UInt32>(sizeof(text) - 1u), text);
	SYMBIOSIS_TEST_CHECK(checkText(text, "3"));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(steppedDefault, "2", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 2.0f / 3.0f));
	SYMBIOSIS_TEST_CHECK(!convertTextToParameterValueDefault(steppedDefault, "4", &value));

	convertParameterValueToTextDefault(steppedCustom, 0.66f, static_cast<UInt32>(sizeof(text) - 1u), text);
	SYMBIOSIS_TEST_CHECK(checkText(text, "Two"));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(steppedCustom, "Three", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 1.0f));
	SYMBIOSIS_TEST_CHECK(!convertTextToParameterValueDefault(steppedCustom, "Missing", &value));

	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(linear, "1000", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 1.0f));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(linear, "-1000", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 0.0f));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(linear, " +5 ", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 0.75f));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(zeroRange, "6", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 0.0f));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(steppedDefault, " 3 ", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 1.0f));
	SYMBIOSIS_TEST_CHECK(!convertTextToParameterValueDefault(steppedDefault, "-1", &value));
	SYMBIOSIS_TEST_CHECK(!convertTextToParameterValueDefault(steppedDefault, "3.0", &value));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(booleanDefault, " TrUe ", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 1.0f));
	SYMBIOSIS_TEST_CHECK(convertTextToParameterValueDefault(booleanDefault, " 0 ", &value));
	SYMBIOSIS_TEST_CHECK(nearEqual(value, 0.0f));
	SYMBIOSIS_TEST_CHECK(!convertTextToParameterValueDefault(booleanDefault, "2", &value));

	convertParameterValueToTextDefault(steppedDefault, 0.1666f, static_cast<UInt32>(sizeof(text) - 1u), text);
	SYMBIOSIS_TEST_CHECK(checkText(text, "0"));
	convertParameterValueToTextDefault(steppedDefault, 0.1667f, static_cast<UInt32>(sizeof(text) - 1u), text);
	SYMBIOSIS_TEST_CHECK(checkText(text, "1"));

	UTF8Z tiny[8];
	memset(tiny, 'X', sizeof(tiny));
	convertParameterValueToTextDefault(booleanDefault, 1.0f, 1u, tiny);
	SYMBIOSIS_TEST_CHECK(tiny[0] == 'O' && tiny[1] == 0);
	memset(tiny, 'X', sizeof(tiny));
	convertParameterValueToTextDefault(booleanDefault, 1.0f, 2u, tiny);
	SYMBIOSIS_TEST_CHECK(tiny[0] == 'O' && tiny[1] == 'n' && tiny[2] == 0);
	memset(tiny, 'X', sizeof(tiny));
	convertParameterValueToTextDefault(steppedCustom, 1.0f, 4u, tiny);
	SYMBIOSIS_TEST_CHECK(tiny[0] == 'T' && tiny[1] == 'h' && tiny[2] == 'r' && tiny[3] == 'e' && tiny[4] == 0);
	memset(tiny, 'X', sizeof(tiny));
	convertParameterValueToTextDefault(linear, 0.0f, 2u, tiny);
	SYMBIOSIS_TEST_CHECK(tiny[0] == '-' && tiny[1] == '1' && tiny[2] == 0);
	memset(tiny, 'X', sizeof(tiny));
	convertParameterValueToTextDefault(linear, 0.0f, 3u, tiny);
	SYMBIOSIS_TEST_CHECK(tiny[0] == '-' && tiny[1] == '1' && tiny[2] == '0' && tiny[3] == 0);
	for (UInt32 maxLength = 1; maxLength <= 8; ++maxLength) {
		UTF8Z b[9];
		memset(b, 'X', sizeof(b));
		convertParameterValueToTextDefault(steppedCustom, 1.0f, maxLength, b);
		bool foundZero = false;
		for (UInt32 i = 0; i <= maxLength; ++i) {
			if (b[i] == 0) {
				foundZero = true;
				break;
			}
		}
		SYMBIOSIS_TEST_CHECK(foundZero);
	}
	for (UInt32 maxLength = 1; maxLength <= 8; ++maxLength) {
		UTF8Z b[9];
		memset(b, 'X', sizeof(b));
		convertParameterValueToTextDefault(linear, 0.0f, maxLength, b);
		bool foundZero = false;
		for (UInt32 i = 0; i <= maxLength; ++i) {
			if (b[i] == 0) {
				foundZero = true;
				break;
			}
		}
		SYMBIOSIS_TEST_CHECK(foundZero);
	}

	return true;
}

static bool runLittleEndianCodecTests() {
	{
		UByte8 bytes16[2] = { 0u, 0u };
		encodeLE16(bytes16, static_cast<UInt16>(0xABCDu));
		SYMBIOSIS_TEST_CHECK(bytes16[0] == 0xCDu);
		SYMBIOSIS_TEST_CHECK(bytes16[1] == 0xABu);
		SYMBIOSIS_TEST_CHECK(decodeLE16(bytes16) == static_cast<UInt16>(0xABCDu));
	}

	{
		UByte8 bytes32[4] = { 0u, 0u, 0u, 0u };
		encodeLE32(bytes32, 0x89ABCDEFu);
		SYMBIOSIS_TEST_CHECK(bytes32[0] == 0xEFu);
		SYMBIOSIS_TEST_CHECK(bytes32[1] == 0xCDu);
		SYMBIOSIS_TEST_CHECK(bytes32[2] == 0xABu);
		SYMBIOSIS_TEST_CHECK(bytes32[3] == 0x89u);
		SYMBIOSIS_TEST_CHECK(decodeLE32(bytes32) == 0x89ABCDEFu);
	}

	{
		UByte8 bytes64[8] = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
		const UInt64 value64 = 0x0123456789ABCDEFuLL;
		encodeLE64(bytes64, value64);
		SYMBIOSIS_TEST_CHECK(bytes64[0] == 0xEFu);
		SYMBIOSIS_TEST_CHECK(bytes64[1] == 0xCDu);
		SYMBIOSIS_TEST_CHECK(bytes64[2] == 0xABu);
		SYMBIOSIS_TEST_CHECK(bytes64[3] == 0x89u);
		SYMBIOSIS_TEST_CHECK(bytes64[4] == 0x67u);
		SYMBIOSIS_TEST_CHECK(bytes64[5] == 0x45u);
		SYMBIOSIS_TEST_CHECK(bytes64[6] == 0x23u);
		SYMBIOSIS_TEST_CHECK(bytes64[7] == 0x01u);
		SYMBIOSIS_TEST_CHECK(decodeLE64(bytes64) == value64);
	}

	{
		const Float32 inputValue = -123.75f;
		UByte8 bytes32[4] = { 0u, 0u, 0u, 0u };
		encodeLEFloat32(bytes32, inputValue);
		const Float32 outputValue = decodeLEFloat32(bytes32);
		SYMBIOSIS_TEST_CHECK(nearEqual(outputValue, inputValue));
	}

	{
		const Double64 inputValue = 123456.75;
		UByte8 bytes64[8] = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
		encodeLEDouble64(bytes64, inputValue);
		const Double64 outputValue = decodeLEDouble64(bytes64);
		const Double64 delta = (outputValue > inputValue ? outputValue - inputValue : inputValue - outputValue);
		SYMBIOSIS_TEST_CHECK(delta <= 1.0e-12);
	}

	return true;
}

static bool runUTFAndTextHandlingTests() {
	UTF8Z appended[12];
	memset(appended, 'X', sizeof(appended));
	appended[0] = 0;
	UInt32 offset = appendText(appended, static_cast<UInt32>(sizeof(appended) - 1u), 0u, "AB");
	SYMBIOSIS_TEST_CHECK(offset == 2u);
	offset = appendText(appended, static_cast<UInt32>(sizeof(appended) - 1u), offset, "CDEF");
	SYMBIOSIS_TEST_CHECK(offset == 6u);
	SYMBIOSIS_TEST_CHECK(checkText(appended, "ABCDEF"));
	offset = appendText(appended, 4u, 3u, "ZZ");
	SYMBIOSIS_TEST_CHECK(offset == 4u);
	SYMBIOSIS_TEST_CHECK(appended[3] == 'Z' && appended[4] == 0);
	appended[4] = 'Q';
	offset = appendText(appended, 4u, 4u, "X");
	SYMBIOSIS_TEST_CHECK(offset == 4u);
	SYMBIOSIS_TEST_CHECK(appended[4] == 0);

	memset(appended, 0, sizeof(appended));
	offset = appendUIntToString(appended, static_cast<UInt32>(sizeof(appended) - 1u), 0u, 0u);
	SYMBIOSIS_TEST_CHECK(offset == 1u);
	SYMBIOSIS_TEST_CHECK(checkText(appended, "0"));
	offset = appendUIntToString(appended, static_cast<UInt32>(sizeof(appended) - 1u), 1u, 12345u);
	SYMBIOSIS_TEST_CHECK(offset == 6u);
	SYMBIOSIS_TEST_CHECK(checkText(appended, "012345"));
	offset = appendUIntToString(appended, 4u, 3u, 9u);
	SYMBIOSIS_TEST_CHECK(offset == 4u);
	offset = appendUIntToString(appended, 4u, 4u, 9u);
	SYMBIOSIS_TEST_CHECK(offset == 4u);

	memset(appended, 'X', sizeof(appended));
	appended[0] = 0;
	offset = appendFloatToString(appended, static_cast<UInt32>(sizeof(appended) - 1u), 0u, 1.25f, 3u);
	SYMBIOSIS_TEST_CHECK(offset == 4u);
	SYMBIOSIS_TEST_CHECK(checkText(appended, "1.25"));
	offset = appendFloatToString(appended, static_cast<UInt32>(sizeof(appended) - 1u), 4u, -0.5f, 2u);
	SYMBIOSIS_TEST_CHECK(offset == 8u);
	SYMBIOSIS_TEST_CHECK(checkText(appended, "1.25-0.5"));
	UTF8Z tinyFloat[2];
	tinyFloat[0] = 'X';
	tinyFloat[1] = 'X';
	offset = appendFloatToString(tinyFloat, 1u, 0u, 9.0f, 3u);
	SYMBIOSIS_TEST_CHECK(offset == 1u);
	SYMBIOSIS_TEST_CHECK(tinyFloat[1] == 0);
	offset = appendFloatToString(tinyFloat, 1u, 1u, 9.0f, 3u);
	SYMBIOSIS_TEST_CHECK(offset == 1u);

	{
		UTF8Z utf8[16];

		memset(utf8, 'X', sizeof(utf8));
		utf8[0] = 0;
		offset = appendText(utf8, 5u, 0u, "Hello");
		SYMBIOSIS_TEST_CHECK(offset == 5u);
		SYMBIOSIS_TEST_CHECK(checkText(utf8, "Hello"));

		memset(utf8, 'X', sizeof(utf8));
		utf8[0] = 0;
		offset = appendText(utf8, 4u, 0u, "Hello");
		SYMBIOSIS_TEST_CHECK(offset == 4u);
		SYMBIOSIS_TEST_CHECK(checkText(utf8, "Hell"));

		memset(utf8, 'X', sizeof(utf8));
		strcpy(utf8, "AB");
		offset = appendText(utf8, 4u, 2u, "\xC2\xA2");
		SYMBIOSIS_TEST_CHECK(offset == 4u);
		SYMBIOSIS_TEST_CHECK(checkText(utf8, "AB\xC2\xA2"));

		memset(utf8, 'X', sizeof(utf8));
		strcpy(utf8, "AB");
		offset = appendText(utf8, 3u, 2u, "\xC2\xA2");
		SYMBIOSIS_TEST_CHECK(offset == 2u);
		SYMBIOSIS_TEST_CHECK(checkText(utf8, "AB"));

		memset(utf8, 'X', sizeof(utf8));
		strcpy(utf8, "A");
		offset = appendText(utf8, 3u, 1u, "\xE2\x82\xAC");
		SYMBIOSIS_TEST_CHECK(offset == 1u);
		SYMBIOSIS_TEST_CHECK(checkText(utf8, "A"));

		memset(utf8, 'X', sizeof(utf8));
		strcpy(utf8, "A");
		offset = appendText(utf8, 4u, 1u, "\xE2\x82\xAC");
		SYMBIOSIS_TEST_CHECK(offset == 4u);
		SYMBIOSIS_TEST_CHECK(checkText(utf8, "A\xE2\x82\xAC"));

		memset(utf8, 'X', sizeof(utf8));
		strcpy(utf8, "A");
		offset = appendText(utf8, 4u, 1u, "\xF0\x9F\x98\x80");
		SYMBIOSIS_TEST_CHECK(offset == 1u);
		SYMBIOSIS_TEST_CHECK(checkText(utf8, "A"));

		memset(utf8, 'X', sizeof(utf8));
		strcpy(utf8, "A");
		offset = appendText(utf8, 5u, 1u, "\xF0\x9F\x98\x80");
		SYMBIOSIS_TEST_CHECK(offset == 5u);
		SYMBIOSIS_TEST_CHECK(checkText(utf8, "A\xF0\x9F\x98\x80"));

		memset(utf8, 'X', sizeof(utf8));
		strcpy(utf8, "ABC");
		offset = appendText(utf8, 3u, 3u, "Z");
		SYMBIOSIS_TEST_CHECK(offset == 3u);
		SYMBIOSIS_TEST_CHECK(checkText(utf8, "ABC"));

		memset(utf8, 'X', sizeof(utf8));
		strcpy(utf8, "AB");
		offset = appendText(utf8, 5u, 2u, "");
		SYMBIOSIS_TEST_CHECK(offset == 2u);
		SYMBIOSIS_TEST_CHECK(checkText(utf8, "AB"));
	}

	{
		static const uint16_t UTF16_MIXED[] = { 0x0041u, 0x00A2u, 0x20ACu, 0xD83Du, 0xDE00u, 0x005Au };
		static const UByte8 UTF8_MIXED[] = { 0x41u, 0xC2u, 0xA2u, 0xE2u, 0x82u, 0xACu, 0xF0u, 0x9Fu, 0x98u, 0x80u, 0x5Au };
		UTF8Z utf8Out[32];
		uint16_t utf16Out[16];

		SYMBIOSIS_TEST_CHECK(calcUTF16ToUTF8Size(6u, UTF16_MIXED) == 11u);
		UInt32 utf8Count = convertUTF16ToUTF8(6u, UTF16_MIXED, utf8Out);
		SYMBIOSIS_TEST_CHECK(utf8Count == 11u);
		SYMBIOSIS_TEST_CHECK(checkUTF8Bytes(utf8Out, UTF8_MIXED, 11u));

		SYMBIOSIS_TEST_CHECK(calcUTF8ToUTF16Size(11u, reinterpret_cast<const UTF8Z*>(UTF8_MIXED)) == 6u);
		UInt32 utf16Count = convertUTF8ToUTF16(11u, reinterpret_cast<const UTF8Z*>(UTF8_MIXED), utf16Out);
		SYMBIOSIS_TEST_CHECK(utf16Count == 6u);
		SYMBIOSIS_TEST_CHECK(checkUTF16Units(utf16Out, UTF16_MIXED, 6u));
	}

	{
		static const uint16_t UTF16_LIMITS[] = { 0x07FFu, 0x0800u, 0xD800u, 0xDC00u, 0xDBFFu, 0xDFFFu };
		static const UByte8 UTF8_LIMITS[] = { 0xDFu, 0xBFu, 0xE0u, 0xA0u, 0x80u, 0xF0u, 0x90u, 0x80u, 0x80u, 0xF4u, 0x8Fu, 0xBFu, 0xBFu };
		UTF8Z utf8Out[32];
		uint16_t utf16Out[16];

		SYMBIOSIS_TEST_CHECK(calcUTF16ToUTF8Size(6u, UTF16_LIMITS) == 13u);
		UInt32 utf8Count = convertUTF16ToUTF8(6u, UTF16_LIMITS, utf8Out);
		SYMBIOSIS_TEST_CHECK(utf8Count == 13u);
		SYMBIOSIS_TEST_CHECK(checkUTF8Bytes(utf8Out, UTF8_LIMITS, 13u));

		SYMBIOSIS_TEST_CHECK(calcUTF8ToUTF16Size(13u, reinterpret_cast<const UTF8Z*>(UTF8_LIMITS)) == 6u);
		UInt32 utf16Count = convertUTF8ToUTF16(13u, reinterpret_cast<const UTF8Z*>(UTF8_LIMITS), utf16Out);
		SYMBIOSIS_TEST_CHECK(utf16Count == 6u);
		SYMBIOSIS_TEST_CHECK(checkUTF16Units(utf16Out, UTF16_LIMITS, 6u));
	}

	{
		const uint16_t utf16Dummy[1] = { 0u };
		UTF8Z utf8Dummy[1] = { 0 };
		uint16_t utf16OutDummy[1] = { 0u };
		SYMBIOSIS_TEST_CHECK(calcUTF16ToUTF8Size(0u, utf16Dummy) == 0u);
		SYMBIOSIS_TEST_CHECK(convertUTF16ToUTF8(0u, utf16Dummy, utf8Dummy) == 0u);
		SYMBIOSIS_TEST_CHECK(calcUTF8ToUTF16Size(0u, utf8Dummy) == 0u);
		SYMBIOSIS_TEST_CHECK(convertUTF8ToUTF16(0u, utf8Dummy, utf16OutDummy) == 0u);
	}

	return true;
}

bool runSelfTest() {
	lastTestFailure[0] = 0;
	SYMBIOSIS_TEST_CHECK(runLittleEndianCodecTests());
	SYMBIOSIS_TEST_CHECK(runUTFAndTextHandlingTests());
	SYMBIOSIS_TEST_CHECK(runParameterTextConversionTests());
	SYMBIOSIS_TEST_CHECK(runDirectedSortingTests());
	SYMBIOSIS_TEST_CHECK(runRandomSortingTests());
	return true;
}

} // namespace symbiosis
