#include "SymbiosisCpp.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <limits.h>
#include <string.h>

namespace symbiosis {

void traceMessage(const UTF8Z* context, const UTF8Z* text) {
	SYMBIOSIS_ASSERT(context != 0);
	SYMBIOSIS_ASSERT(text != 0);
	UTF8Z message[1024];
	UInt32 offset = 0;
	const UInt32 MAX_LENGTH = static_cast<UInt32>(sizeof (message) - 1u);
	offset = appendText(message, MAX_LENGTH, offset, context);
	offset = appendText(message, MAX_LENGTH, offset, ": ");
	appendText(message, MAX_LENGTH, offset, text);
	symbiosisTrace(message);
}

void tracePlugInLastError(PlugInInterface* plugIn, const UTF8Z* context) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	SYMBIOSIS_ASSERT(context != 0);
	UTF8Z errorText[1024];
	errorText[0] = 0;
	plugIn->getLastErrorText(static_cast<UInt32>(sizeof (errorText) - 1u), errorText);
	traceMessage(context, (errorText[0] != 0 ? errorText : "Unknown Symbiosis error"));
}

Bool8 toBool8(bool value) {
	return value ? 1 : 0;
}

UInt16 decodeLE16(const UByte8 bytes[2]) {
	SYMBIOSIS_ASSERT(bytes != 0);
	return static_cast<UInt16>(
			static_cast<UInt16>(bytes[0])
			| (static_cast<UInt16>(bytes[1]) << 8));
}

UInt32 decodeLE32(const UByte8 bytes[4]) {
	SYMBIOSIS_ASSERT(bytes != 0);
	return static_cast<UInt32>(bytes[0])
			| (static_cast<UInt32>(bytes[1]) << 8)
			| (static_cast<UInt32>(bytes[2]) << 16)
			| (static_cast<UInt32>(bytes[3]) << 24);
}

UInt64 decodeLE64(const UByte8 bytes[8]) {
	SYMBIOSIS_ASSERT(bytes != 0);
	return static_cast<UInt64>(bytes[0])
			| (static_cast<UInt64>(bytes[1]) << 8)
			| (static_cast<UInt64>(bytes[2]) << 16)
			| (static_cast<UInt64>(bytes[3]) << 24)
			| (static_cast<UInt64>(bytes[4]) << 32)
			| (static_cast<UInt64>(bytes[5]) << 40)
			| (static_cast<UInt64>(bytes[6]) << 48)
			| (static_cast<UInt64>(bytes[7]) << 56);
}

Float32 decodeLEFloat32(const UByte8 bytes[4]) {
	const UInt32 bits = decodeLE32(bytes);
	Float32 value = 0.0f;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

Double64 decodeLEDouble64(const UByte8 bytes[8]) {
	const UInt64 bits = decodeLE64(bytes);
	Double64 value = 0.0;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

void encodeLE16(UByte8 bytes[2], UInt16 value) {
	SYMBIOSIS_ASSERT(bytes != 0);
	bytes[0] = static_cast<UByte8>((value >> 0) & 0xFFu);
	bytes[1] = static_cast<UByte8>((value >> 8) & 0xFFu);
}

void encodeLE32(UByte8 bytes[4], UInt32 value) {
	SYMBIOSIS_ASSERT(bytes != 0);
	bytes[0] = static_cast<UByte8>((value >> 0) & 0xFFu);
	bytes[1] = static_cast<UByte8>((value >> 8) & 0xFFu);
	bytes[2] = static_cast<UByte8>((value >> 16) & 0xFFu);
	bytes[3] = static_cast<UByte8>((value >> 24) & 0xFFu);
}

void encodeLE64(UByte8 bytes[8], UInt64 value) {
	SYMBIOSIS_ASSERT(bytes != 0);
	bytes[0] = static_cast<UByte8>((value >> 0) & 0xFFu);
	bytes[1] = static_cast<UByte8>((value >> 8) & 0xFFu);
	bytes[2] = static_cast<UByte8>((value >> 16) & 0xFFu);
	bytes[3] = static_cast<UByte8>((value >> 24) & 0xFFu);
	bytes[4] = static_cast<UByte8>((value >> 32) & 0xFFu);
	bytes[5] = static_cast<UByte8>((value >> 40) & 0xFFu);
	bytes[6] = static_cast<UByte8>((value >> 48) & 0xFFu);
	bytes[7] = static_cast<UByte8>((value >> 56) & 0xFFu);
}

void encodeLEFloat32(UByte8 bytes[4], Float32 value) {
	UInt32 bits = 0u;
	memcpy(&bits, &value, sizeof(bits));
	encodeLE32(bytes, bits);
}

void encodeLEDouble64(UByte8 bytes[8], Double64 value) {
	UInt64 bits = 0u;
	memcpy(&bits, &value, sizeof(bits));
	encodeLE64(bytes, bits);
}

static bool isNormalizedValue(Float32 value) {
	return value >= 0.0f && value <= 1.0f;
}

static bool isCanonicalBool8(Bool8 value) {
	return value == 0 || value == 1;
}

static bool isNullTerminatedWithin(const UTF8Z* text, UInt32 maxLength) {
	SYMBIOSIS_ASSERT(text != 0);
	for (UInt32 i = 0; i <= maxLength; ++i) {
		if (text[i] == 0) {
			return true;
		}
	}
	return false;
}

UInt32 busFormatMaskToChannelCount(UInt32 formatMask) {
	SYMBIOSIS_ASSERT(formatMask != 0u);
	SYMBIOSIS_ASSERT((formatMask & (formatMask - 1u)) == 0u);
	switch (formatMask) {
		case SYMBIOSIS_BUS_FORMAT_MONO_MASK: return 1u;
		case SYMBIOSIS_BUS_FORMAT_STEREO_MASK: return 2u;
		case SYMBIOSIS_BUS_FORMAT_LCR_MASK: return 3u;
		case SYMBIOSIS_BUS_FORMAT_QUAD_MASK: return 4u;
		case SYMBIOSIS_BUS_FORMAT_5_0_MASK: return 5u;
		case SYMBIOSIS_BUS_FORMAT_5_1_MASK: return 6u;
		case SYMBIOSIS_BUS_FORMAT_6_0_CINE_MASK: return 6u;
		case SYMBIOSIS_BUS_FORMAT_6_1_CINE_MASK: return 7u;
		case SYMBIOSIS_BUS_FORMAT_7_0_CINE_MASK: return 7u;
		case SYMBIOSIS_BUS_FORMAT_7_1_CINE_MASK: return 8u;
		case SYMBIOSIS_BUS_FORMAT_7_1_MUSIC_MASK: return 8u;
		case SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK: return 2u;
		default: break;
	}
	SYMBIOSIS_ASSERT(0);
	return 2u;
}

UInt32 appendText(UTF8Z* outText, UInt32 maxLength, UInt32 offset, const UTF8Z* source) {
	SYMBIOSIS_ASSERT(outText != 0);
	SYMBIOSIS_ASSERT(source != 0);
	SYMBIOSIS_ASSERT(offset <= maxLength);

	UInt32 i = 0;
	while (offset + i <= maxLength) {
		outText[offset + i] = source[i];
		if (source[i] == 0) {
			return offset + i;
		}
		++i;
	}

	i = maxLength;
	while (i > offset && (static_cast<unsigned char>(outText[i]) & 0xC0u) == 0x80u) {
		--i;
	}

	outText[i] = 0;
	return i;
}

UInt32 appendUIntToString(UTF8Z* outText, UInt32 maxLength, UInt32 offset, UInt32 value) {
	SYMBIOSIS_ASSERT(outText != 0);
	SYMBIOSIS_ASSERT(offset <= maxLength);
	if (offset == maxLength) {
		outText[offset] = 0;
		return offset;
	}
	char digits[10];
	UInt32 digitCount = 0;
	do {
		digits[digitCount] = static_cast<char>('0' + (value % 10));
		value /= 10;
		++digitCount;
	} while (value != 0 && digitCount < static_cast<UInt32>(sizeof(digits)));
	for (UInt32 i = 0; i < digitCount && offset < maxLength; ++i) {
		outText[offset] = digits[digitCount - 1 - i];
		++offset;
	}
	outText[offset] = 0;
	return offset;
}

UInt32 appendFloatToString(UTF8Z* outText, UInt32 maxLength, UInt32 offset, Float32 value, UInt32 maxPrecision) {
	SYMBIOSIS_ASSERT(outText != 0);
	SYMBIOSIS_ASSERT(offset <= maxLength);
	if (offset == maxLength) {
		outText[offset] = 0;
		return offset;
	}
	SYMBIOSIS_ASSERT(maxPrecision <= 9u);
	double absoluteValue = static_cast<double>(value);
	if (value < 0.0f && offset < maxLength) {
		outText[offset] = '-';
		++offset;
		absoluteValue = -absoluteValue;
	}
	static const UInt32 POW10[10] = { 1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u, 100000000u, 1000000000u };
	const UInt32 scale = POW10[maxPrecision];
	SYMBIOSIS_ASSERT(absoluteValue == absoluteValue);
	SYMBIOSIS_ASSERT(absoluteValue <= 4294967295.0);
	const uint64_t roundedScaledValue = static_cast<uint64_t>(absoluteValue * static_cast<double>(scale) + 0.5);
	const UInt32 integerPart = static_cast<UInt32>(roundedScaledValue / static_cast<uint64_t>(scale));
	UInt32 fractionalPart = static_cast<UInt32>(roundedScaledValue % static_cast<uint64_t>(scale));
	offset = appendUIntToString(outText, maxLength, offset, integerPart);
	if (fractionalPart != 0u && maxPrecision > 0u && offset < maxLength) {
		outText[offset] = '.';
		++offset;
		UInt32 divisor = scale;
		UInt32 i = 0;
		while (i < maxPrecision && fractionalPart != 0u && offset < maxLength) {
			divisor /= 10u;
			const UInt32 digit = fractionalPart / divisor;
			outText[offset] = static_cast<char>('0' + digit);
			++offset;
			fractionalPart %= divisor;
			++i;
		}
	}
	outText[offset] = 0;
	return offset;
}

UInt32 calcUTF16ToUTF8Size(UInt32 utf16Size, const UInt16* utf16Chars) {
	SYMBIOSIS_ASSERT(utf16Chars != 0);
	UInt32 utf8Size = 0u;
	for (UInt32 i = 0u; i < utf16Size; ++i) {
		const UInt16 c = utf16Chars[i];
		if (c < 0x80u) {
			utf8Size += 1u;
		} else if (c < 0x800u) {
			utf8Size += 2u;
		} else if (c >= 0xD800u && c < 0xE000u) {
			SYMBIOSIS_ASSERT(c < 0xDC00u);
			++i;
			SYMBIOSIS_ASSERT(i < utf16Size);
			SYMBIOSIS_ASSERT(utf16Chars[i] >= 0xDC00u && utf16Chars[i] < 0xE000u);
			utf8Size += 4u;
		} else {
			utf8Size += 3u;
		}
	}
	return utf8Size;
}

UInt32 convertUTF16ToUTF8(UInt32 utf16Size, const UInt16* utf16Chars, UTF8Z* utf8Chars) {
	SYMBIOSIS_ASSERT(utf16Chars != 0);
	SYMBIOSIS_ASSERT(utf8Chars != 0);
	UInt32 outIndex = 0u;
	for (UInt32 i = 0u; i < utf16Size; ++i) {
		const UInt16 c = utf16Chars[i];
		if (c < 0x80u) {
			utf8Chars[outIndex + 0u] = static_cast<UTF8Z>(c);
			outIndex += 1u;
		} else if (c < 0x800u) {
			utf8Chars[outIndex + 0u] = static_cast<UTF8Z>((c >> 6u) | 0xC0u);
			utf8Chars[outIndex + 1u] = static_cast<UTF8Z>((c & 0x3Fu) | 0x80u);
			outIndex += 2u;
		} else if (c >= 0xD800u && c < 0xE000u) {
			SYMBIOSIS_ASSERT(c < 0xDC00u);
			++i;
			SYMBIOSIS_ASSERT(i < utf16Size);
			const UInt16 d = utf16Chars[i];
			SYMBIOSIS_ASSERT(d >= 0xDC00u && d < 0xE000u);
			const uint32_t c32 = 0x10000u + (static_cast<uint32_t>(c - 0xD800u) << 10u)
					+ static_cast<uint32_t>(d - 0xDC00u);
			utf8Chars[outIndex + 0u] = static_cast<UTF8Z>((c32 >> 18u) | 0xF0u);
			utf8Chars[outIndex + 1u] = static_cast<UTF8Z>(((c32 >> 12u) & 0x3Fu) | 0x80u);
			utf8Chars[outIndex + 2u] = static_cast<UTF8Z>(((c32 >> 6u) & 0x3Fu) | 0x80u);
			utf8Chars[outIndex + 3u] = static_cast<UTF8Z>((c32 & 0x3Fu) | 0x80u);
			outIndex += 4u;
		} else {
			utf8Chars[outIndex + 0u] = static_cast<UTF8Z>((c >> 12u) | 0xE0u);
			utf8Chars[outIndex + 1u] = static_cast<UTF8Z>(((c >> 6u) & 0x3Fu) | 0x80u);
			utf8Chars[outIndex + 2u] = static_cast<UTF8Z>((c & 0x3Fu) | 0x80u);
			outIndex += 3u;
		}
	}
	return outIndex;
}

UInt32 calcUTF8ToUTF16Size(UInt32 utf8Size, const UTF8Z* utf8Chars) {
	SYMBIOSIS_ASSERT(utf8Chars != 0);
	UInt32 utf16Size = 0u;
	for (UInt32 i = 0u; i < utf8Size; ++i) {
		const unsigned char c = static_cast<unsigned char>(utf8Chars[i]);
		if ((c & 0x80u) == 0u) {
			utf16Size += 1u;
		} else if ((c & 0xE0u) == 0xC0u) {
			SYMBIOSIS_ASSERT(i + 1u < utf8Size);
			SYMBIOSIS_ASSERT((static_cast<unsigned char>(utf8Chars[i + 1u]) & 0xC0u) == 0x80u);
			SYMBIOSIS_ASSERT(c >= 0xC2u);
			utf16Size += 1u;
			i += 1u;
		} else if ((c & 0xF0u) == 0xE0u) {
			SYMBIOSIS_ASSERT(i + 2u < utf8Size);
			SYMBIOSIS_ASSERT((static_cast<unsigned char>(utf8Chars[i + 1u]) & 0xC0u) == 0x80u
					&& (static_cast<unsigned char>(utf8Chars[i + 2u]) & 0xC0u) == 0x80u);
			SYMBIOSIS_ASSERT(c != 0xE0u || static_cast<unsigned char>(utf8Chars[i + 1u]) >= 0xA0u);
			SYMBIOSIS_ASSERT(c != 0xEDu || static_cast<unsigned char>(utf8Chars[i + 1u]) <= 0x9Fu);
			utf16Size += 1u;
			i += 2u;
		} else if ((c & 0xF8u) == 0xF0u) {
			SYMBIOSIS_ASSERT(i + 3u < utf8Size);
			SYMBIOSIS_ASSERT((static_cast<unsigned char>(utf8Chars[i + 1u]) & 0xC0u) == 0x80u
					&& (static_cast<unsigned char>(utf8Chars[i + 2u]) & 0xC0u) == 0x80u
					&& (static_cast<unsigned char>(utf8Chars[i + 3u]) & 0xC0u) == 0x80u);
			SYMBIOSIS_ASSERT(c >= 0xF0u && c <= 0xF4u);
			SYMBIOSIS_ASSERT(c != 0xF0u || static_cast<unsigned char>(utf8Chars[i + 1u]) >= 0x90u);
			SYMBIOSIS_ASSERT(c != 0xF4u || static_cast<unsigned char>(utf8Chars[i + 1u]) <= 0x8Fu);
			utf16Size += 2u;
			i += 3u;
		} else {
			SYMBIOSIS_ASSERT(0);
		}
	}
	return utf16Size;
}

UInt32 convertUTF8ToUTF16(UInt32 utf8Size, const UTF8Z* utf8Chars, UInt16* utf16Chars) {
	SYMBIOSIS_ASSERT(utf8Chars != 0);
	SYMBIOSIS_ASSERT(utf16Chars != 0);
	UInt32 outIndex = 0u;
	for (UInt32 i = 0u; i < utf8Size; ) {
		const unsigned char c = static_cast<unsigned char>(utf8Chars[i]);
		if ((c & 0x80u) == 0u) {
			utf16Chars[outIndex++] = static_cast<UInt16>(c);
			i += 1u;
		} else if ((c & 0xE0u) == 0xC0u) {
			SYMBIOSIS_ASSERT(i + 1u < utf8Size);
			SYMBIOSIS_ASSERT((static_cast<unsigned char>(utf8Chars[i + 1u]) & 0xC0u) == 0x80u);
			SYMBIOSIS_ASSERT(c >= 0xC2u);
			const UInt16 c16 = static_cast<UInt16>(((c & 0x1Fu) << 6u)
					| (static_cast<unsigned char>(utf8Chars[i + 1u]) & 0x3Fu));
			utf16Chars[outIndex++] = c16;
			i += 2u;
		} else if ((c & 0xF0u) == 0xE0u) {
			SYMBIOSIS_ASSERT(i + 2u < utf8Size);
			SYMBIOSIS_ASSERT((static_cast<unsigned char>(utf8Chars[i + 1u]) & 0xC0u) == 0x80u
					&& (static_cast<unsigned char>(utf8Chars[i + 2u]) & 0xC0u) == 0x80u);
			SYMBIOSIS_ASSERT(c != 0xE0u || static_cast<unsigned char>(utf8Chars[i + 1u]) >= 0xA0u);
			SYMBIOSIS_ASSERT(c != 0xEDu || static_cast<unsigned char>(utf8Chars[i + 1u]) <= 0x9Fu);
			const UInt16 c16 = static_cast<UInt16>(((c & 0x0Fu) << 12u)
					| ((static_cast<unsigned char>(utf8Chars[i + 1u]) & 0x3Fu) << 6u)
					| (static_cast<unsigned char>(utf8Chars[i + 2u]) & 0x3Fu));
			utf16Chars[outIndex++] = c16;
			i += 3u;
		} else if ((c & 0xF8u) == 0xF0u) {
			SYMBIOSIS_ASSERT(i + 3u < utf8Size);
			SYMBIOSIS_ASSERT((static_cast<unsigned char>(utf8Chars[i + 1u]) & 0xC0u) == 0x80u
					&& (static_cast<unsigned char>(utf8Chars[i + 2u]) & 0xC0u) == 0x80u
					&& (static_cast<unsigned char>(utf8Chars[i + 3u]) & 0xC0u) == 0x80u);
			SYMBIOSIS_ASSERT(c >= 0xF0u && c <= 0xF4u);
			SYMBIOSIS_ASSERT(c != 0xF0u || static_cast<unsigned char>(utf8Chars[i + 1u]) >= 0x90u);
			SYMBIOSIS_ASSERT(c != 0xF4u || static_cast<unsigned char>(utf8Chars[i + 1u]) <= 0x8Fu);
			const uint32_t c32 = (static_cast<uint32_t>(c & 0x07u) << 18u)
					| (static_cast<uint32_t>(static_cast<unsigned char>(utf8Chars[i + 1u]) & 0x3Fu) << 12u)
					| (static_cast<uint32_t>(static_cast<unsigned char>(utf8Chars[i + 2u]) & 0x3Fu) << 6u)
					| static_cast<uint32_t>(static_cast<unsigned char>(utf8Chars[i + 3u]) & 0x3Fu);
			utf16Chars[outIndex + 0u] = static_cast<UInt16>(((c32 - 0x10000u) >> 10u) + 0xD800u);
			utf16Chars[outIndex + 1u] = static_cast<UInt16>(((c32 - 0x10000u) & 0x3FFu) + 0xDC00u);
			outIndex += 2u;
			i += 4u;
		} else {
			SYMBIOSIS_ASSERT(0);
		}
	}
	return outIndex;
}

static const char* skipSpaces(const char* text) {
	SYMBIOSIS_ASSERT(text != 0);
	while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
		++text;
	}
	return text;
}

static bool isAsciiDigit(char c) {
	return c >= '0' && c <= '9';
}

static char toLowerAscii(char c) {
	if (c >= 'A' && c <= 'Z') {
		return static_cast<char>(c - 'A' + 'a');
	}
	return c;
}

static bool equalsAsciiCaseInsensitiveTrimmed(const char* text, const char* word) {
	SYMBIOSIS_ASSERT(text != 0);
	SYMBIOSIS_ASSERT(word != 0);
	const char* p = skipSpaces(text);
	while (*p != 0 && *word != 0) {
		if (toLowerAscii(*p) != toLowerAscii(*word)) {
			return false;
		}
		++p;
		++word;
	}
	if (*word != 0) {
		return false;
	}
	p = skipSpaces(p);
	return *p == 0;
}

bool parseUnsignedIntegerText(const UTF8Z* text, UInt32* outValue) {
	SYMBIOSIS_ASSERT(text != 0);
	SYMBIOSIS_ASSERT(outValue != 0);
	const char* p = skipSpaces(text);
	if (!isAsciiDigit(*p)) {
		return false;
	}
	UInt32 value = 0;
	while (isAsciiDigit(*p)) {
		const UInt32 digit = static_cast<UInt32>(*p - '0');
		if (value > UINT_MAX / 10u || (value == UINT_MAX / 10u && digit > UINT_MAX % 10u)) {
			return false;
		}
		value = value * 10u + digit;
		++p;
	}
	p = skipSpaces(p);
	if (*p != 0) {
		return false;
	}
	*outValue = value;
	return true;
}

bool parseSignedDecimalText(const UTF8Z* text, Float32* outValue) {
	SYMBIOSIS_ASSERT(text != 0);
	SYMBIOSIS_ASSERT(outValue != 0);
	const char* p = skipSpaces(text);
	bool negative = false;
	if (*p == '+' || *p == '-') {
		negative = (*p == '-');
		++p;
	}
	if (!isAsciiDigit(*p) && *p != '.') {
		return false;
	}
	double integerPart = 0.0;
	bool hasDigit = false;
	while (isAsciiDigit(*p)) {
		hasDigit = true;
		integerPart = integerPart * 10.0 + static_cast<double>(*p - '0');
		++p;
	}
	double fractionalPart = 0.0;
	double divisor = 1.0;
	if (*p == '.') {
		++p;
		while (isAsciiDigit(*p)) {
			hasDigit = true;
			fractionalPart = fractionalPart * 10.0 + static_cast<double>(*p - '0');
			divisor *= 10.0;
			++p;
		}
	}
	if (!hasDigit) {
		return false;
	}
	p = skipSpaces(p);
	if (*p != 0) {
		return false;
	}
	double value = integerPart + (divisor > 1.0 ? fractionalPart / divisor : 0.0);
	if (negative) {
		value = -value;
	}
	*outValue = static_cast<Float32>(value);
	return true;
}

void convertParameterValueToTextDefault(const SymbiosisParameterInfo& parameterInfo, Float32 normalizedValue
		, UInt32 maxLength, UTF8Z* outText) {
	SYMBIOSIS_ASSERT(0.0f <= normalizedValue && normalizedValue <= 1.0f);
	SYMBIOSIS_ASSERT(maxLength > 0);
	SYMBIOSIS_ASSERT(outText != 0);
	outText[0] = 0;
	switch (parameterInfo.scale) {
		case SYMBIOSIS_PARAMETER_SCALE_CUSTOM:
		case SYMBIOSIS_PARAMETER_SCALE_LINEAR: {
			const Float32 displayValue = parameterInfo.minimum + normalizedValue * (parameterInfo.maximum - parameterInfo.minimum);
			appendFloatToString(outText, maxLength, 0, displayValue, 3u);
			break;
		}

		case SYMBIOSIS_PARAMETER_SCALE_STEPPED: {
			const UInt32 stepIndex = static_cast<UInt32>(normalizedValue * static_cast<UInt32>(parameterInfo.maximum) + 0.5f);
			if (parameterInfo.displayTexts != 0) {
				SYMBIOSIS_ASSERT(parameterInfo.displayTexts[stepIndex] != 0);
				appendText(outText, maxLength, 0, parameterInfo.displayTexts[stepIndex]);
			} else {
				appendUIntToString(outText, maxLength, 0, stepIndex);
			}
			break;
		}
		
		case SYMBIOSIS_PARAMETER_SCALE_BOOLEAN: {
			const UInt32 state = normalizedValue < 0.5f ? 0u : 1u;
			if (parameterInfo.displayTexts != 0) {
				SYMBIOSIS_ASSERT(parameterInfo.displayTexts[0] != 0);
				SYMBIOSIS_ASSERT(parameterInfo.displayTexts[1] != 0);
				appendText(outText, maxLength, 0, parameterInfo.displayTexts[state]);
			} else {
				appendText(outText, maxLength, 0, state == 0 ? "Off" : "On");
			}
			break;
		}
		
		default: SYMBIOSIS_ASSERT(0);
	}
}

bool convertTextToParameterValueDefault(const SymbiosisParameterInfo& parameterInfo, const UTF8Z* text, Float32* outNormalizedValue) {
	SYMBIOSIS_ASSERT(text != 0);
	SYMBIOSIS_ASSERT(outNormalizedValue != 0);
	if (parameterInfo.scale == SYMBIOSIS_PARAMETER_SCALE_BOOLEAN) {
		if (parameterInfo.displayTexts != 0) {
			SYMBIOSIS_ASSERT(parameterInfo.displayTexts[0] != 0);
			SYMBIOSIS_ASSERT(parameterInfo.displayTexts[1] != 0);
			if (strcmp(text, parameterInfo.displayTexts[0]) == 0) {
				*outNormalizedValue = 0.0f;
				return true;
			}
			if (strcmp(text, parameterInfo.displayTexts[1]) == 0) {
				*outNormalizedValue = 1.0f;
				return true;
			}
		}
		if (equalsAsciiCaseInsensitiveTrimmed(text, "off")
				|| equalsAsciiCaseInsensitiveTrimmed(text, "false")
				|| equalsAsciiCaseInsensitiveTrimmed(text, "no")
				|| equalsAsciiCaseInsensitiveTrimmed(text, "0")) {
			*outNormalizedValue = 0.0f;
			return true;
		}
		if (equalsAsciiCaseInsensitiveTrimmed(text, "on")
				|| equalsAsciiCaseInsensitiveTrimmed(text, "true")
				|| equalsAsciiCaseInsensitiveTrimmed(text, "yes")
				|| equalsAsciiCaseInsensitiveTrimmed(text, "1")) {
			*outNormalizedValue = 1.0f;
			return true;
		}
		return false;
	}
	if (parameterInfo.scale == SYMBIOSIS_PARAMETER_SCALE_STEPPED) {
		const UInt32 stepCount = parameterInfo.maximum > 0.0f ? static_cast<UInt32>(parameterInfo.maximum) + 1u : 0u;
		SYMBIOSIS_ASSERT(stepCount > 0);
		if (parameterInfo.displayTexts != 0) {
			for (UInt32 step = 0; step < stepCount; ++step) {
				SYMBIOSIS_ASSERT(parameterInfo.displayTexts[step] != 0);
				if (strcmp(text, parameterInfo.displayTexts[step]) == 0) {
					*outNormalizedValue = stepCount > 1 ? static_cast<Float32>(step) / static_cast<Float32>(stepCount - 1) : 0.0f;
					return true;
				}
			}
		}
		UInt32 stepIndex = 0;
		if (!parseUnsignedIntegerText(text, &stepIndex) || stepIndex >= stepCount) {
			return false;
		}
		*outNormalizedValue = stepCount > 1 ? static_cast<Float32>(stepIndex) / static_cast<Float32>(stepCount - 1) : 0.0f;
		return true;
	}
	Float32 displayValue = 0.0f;
	if (!parseSignedDecimalText(text, &displayValue)) {
		return false;
	}
	const Float32 range = parameterInfo.maximum - parameterInfo.minimum;
	if (range == 0.0f) {
		*outNormalizedValue = 0.0f;
		return true;
	}
	*outNormalizedValue = std::min(std::max(((displayValue - parameterInfo.minimum) / range), 0.0f), 1.0f);
	return true;
}

CommonBase::CommonBase() {
	lastErrorText[0] = 0;
}

void CommonBase::getLastError(UInt32 maxLength, UTF8Z* outText) {
	SYMBIOSIS_ASSERT(maxLength > 0);
	SYMBIOSIS_ASSERT(outText != 0);
	appendText(outText, maxLength, 0, lastErrorText);
}

void CommonBase::recatch() {
	try {
		throw;
	}
	catch (const std::exception& ex) {
		setLastError(ex.what());
	}
	catch (...) {
		setLastError("Unknown exception");
	}
}

void CommonBase::setLastError(const UTF8Z* text) {
	SYMBIOSIS_ASSERT(text != 0);
	appendText(lastErrorText, 1023u, 0, text);
}

CommonBase::~CommonBase() {}

class PlugIn::CHostInterface : public HostInterface {
	public:
		CHostInterface();

		virtual void updateDisplay() override;
		virtual void beginEdit(UInt32 parameterNumber) override;
		virtual void writeParameter(UInt32 parameterNumber, Float32 normalizedValue) override;
		virtual void endEdit(UInt32 parameterNumber) override;
		virtual bool requestResize(UInt32 width, UInt32 height) override;
		virtual const void* queryExtension(UInt32 vendorId, UInt32 interfaceId) override;

	private:
		friend class Factory;
		CHostInterface(::SymbiosisHost* instance, const ::SymbiosisHostInterface* api);

		::SymbiosisHost* instance;
		const ::SymbiosisHostInterface* api;
};

PlugIn::CHostInterface::CHostInterface() : instance(0), api(0) {}

PlugIn::CHostInterface::CHostInterface(::SymbiosisHost* inInstance, const ::SymbiosisHostInterface* inApi)
	: instance(inInstance), api(inApi) {}

void PlugIn::CHostInterface::updateDisplay() {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	api->updateDisplay(instance);
}

void PlugIn::CHostInterface::beginEdit(UInt32 parameterNumber) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	api->beginEdit(instance, parameterNumber);
}

void PlugIn::CHostInterface::writeParameter(UInt32 parameterNumber, Float32 normalizedValue) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	api->writeParameter(instance, parameterNumber, normalizedValue);
}

void PlugIn::CHostInterface::endEdit(UInt32 parameterNumber) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	api->endEdit(instance, parameterNumber);
}

bool PlugIn::CHostInterface::requestResize(UInt32 width, UInt32 height) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	const Bool8 result = api->requestResize(instance, width, height);
	SYMBIOSIS_ASSERT(isCanonicalBool8(result));
	return result != 0;
}

const void* PlugIn::CHostInterface::queryExtension(UInt32 vendorId, UInt32 interfaceId) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	return api->queryExtension(instance, vendorId, interfaceId);
}

PlugIn::PlugIn(HostInterface* hostInterface)
	: hostInterface(hostInterface) {}

HostInterface* PlugIn::host() const {
	return hostInterface;
}

void PlugIn::adoptHostInterface(std::unique_ptr<HostInterface> hostInterface) {
	SYMBIOSIS_ASSERT(hostInterface.get() != 0);
	SYMBIOSIS_ASSERT(this->hostInterface == hostInterface.get());
	ownedHostInterface = std::move(hostInterface);
}

bool PlugIn::convertTextToParameterValue(UInt32 parameterNumber, const UTF8Z* text, Float32* normalizedValue) {
	(void)parameterNumber;
	SYMBIOSIS_ASSERT(text != 0);
	SYMBIOSIS_ASSERT(normalizedValue != 0);
	(void)text;
	(void)normalizedValue;
	return false;
}

bool PlugIn::convertParameterValueToText(UInt32 parameterNumber, Float32 normalizedValue, UInt32 maxLength, UTF8Z* outText) {
	(void)parameterNumber;
	(void)normalizedValue;
	SYMBIOSIS_ASSERT(outText != 0);
	if (maxLength > 0) {
		outText[0] = 0;
	}
	return false;
}

bool PlugIn::getUIViewSize(UInt32* outWidth, UInt32* outHeight) {
	SYMBIOSIS_ASSERT(outWidth != 0);
	SYMBIOSIS_ASSERT(outHeight != 0);
	*outWidth = 0;
	*outHeight = 0;
	return false;
}

bool PlugIn::openUIView(void* nativeGUIElement) {
	SYMBIOSIS_ASSERT(nativeGUIElement != 0);
	(void)nativeGUIElement;
	return false;
}

void PlugIn::closeUIView() {}

const void* PlugIn::queryExtension(UInt32 vendorId, UInt32 interfaceId) {
	(void)vendorId;
	(void)interfaceId;
	return 0;
}

void PlugIn::getLastErrorText(UInt32 maxLength, UTF8Z* outText) {
	CommonBase::getLastError(maxLength, outText);
}

::SymbiosisPlugIn* PlugIn::getABIInstance() {
	return reinterpret_cast<::SymbiosisPlugIn*>(this);
}

const ::SymbiosisPlugInInterface* PlugIn::getABIInterface() {
	return &abiInterface;
}

void SYMBIOSIS_CALL PlugIn::changeProgramThunk(::SymbiosisPlugIn* plugIn, UInt32 programNumber) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		self->changeProgram(programNumber);
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
	}
}

void SYMBIOSIS_CALL PlugIn::updateParameterThunk(::SymbiosisPlugIn* plugIn, UInt32 parameterNumber, Float32 normalizedValue) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		self->updateParameter(parameterNumber, normalizedValue);
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
	}
}

Float32 SYMBIOSIS_CALL PlugIn::getParameterThunk(::SymbiosisPlugIn* plugIn, UInt32 parameterNumber) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		const Float32 value = self->getParameter(parameterNumber);
		SYMBIOSIS_ASSERT(isNormalizedValue(value));
		return value;
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
		return 0.0f;
	}
}

void SYMBIOSIS_CALL PlugIn::setBypassThunk(::SymbiosisPlugIn* plugIn, Bool8 bypassed) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		self->setBypass(bypassed);
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
	}
}

void SYMBIOSIS_CALL PlugIn::setProgramNameThunk(::SymbiosisPlugIn* plugIn, UInt32 programNumber, const UTF8Z* text) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	SYMBIOSIS_ASSERT(text != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		self->setProgramName(programNumber, text);
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
	}
}

void SYMBIOSIS_CALL PlugIn::getProgramNameThunk(::SymbiosisPlugIn* plugIn, UInt32 programNumber, UInt32 maxLength, UTF8Z* outText) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	SYMBIOSIS_ASSERT(outText != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		self->getProgramName(programNumber, maxLength, outText);
		if (maxLength > 0) {
			SYMBIOSIS_ASSERT(isNullTerminatedWithin(outText, maxLength));
		}
	}
	catch (...) {
		if (maxLength > 0) {
			outText[0] = 0;
		}
		self->recatch();
		SYMBIOSIS_ASSERT(0);
	}
}

Bool8 SYMBIOSIS_CALL PlugIn::convertTextToParameterValueThunk(::SymbiosisPlugIn* plugIn, UInt32 parameterNumber, const UTF8Z* text, Float32* normalizedValue) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	SYMBIOSIS_ASSERT(text != 0);
	SYMBIOSIS_ASSERT(normalizedValue != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		const Bool8 result = toBool8(self->convertTextToParameterValue(parameterNumber, text, normalizedValue));
		if (result != 0) {
			SYMBIOSIS_ASSERT(isNormalizedValue(*normalizedValue));
		}
		return result;
	}
	catch (...) {
		self->recatch();
		return 0;
	}
}

Bool8 SYMBIOSIS_CALL PlugIn::convertParameterValueToTextThunk(::SymbiosisPlugIn* plugIn, UInt32 parameterNumber
		, Float32 normalizedValue, UInt32 maxLength, UTF8Z* outText) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	SYMBIOSIS_ASSERT(outText != 0);
	SYMBIOSIS_ASSERT(maxLength > 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		outText[0] = 0;
		const Bool8 result = toBool8(self->convertParameterValueToText(parameterNumber, normalizedValue, maxLength, outText));
		SYMBIOSIS_ASSERT(isNullTerminatedWithin(outText, maxLength));
		return result;
	}
	catch (...) {
		self->recatch();
		return 0;
	}
}

Bool8 SYMBIOSIS_CALL PlugIn::loadStateThunk(::SymbiosisPlugIn* plugIn, UInt32 dataSize, const UByte8* data) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	SYMBIOSIS_ASSERT(dataSize == 0 || data != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		return toBool8(self->loadState(dataSize, data));
	}
	catch (...) {
		self->recatch();
		return 0;
	}
}

Bool8 SYMBIOSIS_CALL PlugIn::createSaveStateThunk(::SymbiosisPlugIn* plugIn, UInt32* dataSize, UByte8** data) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	SYMBIOSIS_ASSERT(dataSize != 0);
	SYMBIOSIS_ASSERT(data != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		*dataSize = 0;
		*data = 0;
		const bool result = self->createSaveState(dataSize, data);
		if (result) {
			SYMBIOSIS_ASSERT(*dataSize == 0 || *data != 0);
		} else {
			SYMBIOSIS_ASSERT(*dataSize == 0 && *data == 0);
		}
		return toBool8(result);
	}
	catch (...) {
		self->recatch();
		return 0;
	}
}

Bool8 SYMBIOSIS_CALL PlugIn::loadProgramStateThunk(::SymbiosisPlugIn* plugIn, UInt32 programNumber, UInt32 dataSize, const UByte8* data) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	SYMBIOSIS_ASSERT(dataSize == 0 || data != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		return toBool8(self->loadProgramState(programNumber, dataSize, data));
	}
	catch (...) {
		self->recatch();
		return 0;
	}
}

void SYMBIOSIS_CALL PlugIn::destroySaveStateThunk(::SymbiosisPlugIn* plugIn, UByte8* data) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		self->destroySaveState(data);
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
	}
}

Bool8 SYMBIOSIS_CALL PlugIn::createProgramSaveStateThunk(::SymbiosisPlugIn* plugIn, UInt32 programNumber, UInt32* dataSize, UByte8** data) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	SYMBIOSIS_ASSERT(dataSize != 0);
	SYMBIOSIS_ASSERT(data != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		*dataSize = 0;
		*data = 0;
		const bool result = self->createProgramSaveState(programNumber, dataSize, data);
		if (result) {
			SYMBIOSIS_ASSERT(*dataSize == 0 || *data != 0);
		} else {
			SYMBIOSIS_ASSERT(*dataSize == 0 && *data == 0);
		}
		return toBool8(result);
	}
	catch (...) {
		self->recatch();
		return 0;
	}
}

Bool8 SYMBIOSIS_CALL PlugIn::getUIViewSizeThunk(::SymbiosisPlugIn* plugIn, UInt32* outWidth, UInt32* outHeight) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	SYMBIOSIS_ASSERT(outWidth != 0);
	SYMBIOSIS_ASSERT(outHeight != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		const Bool8 result = toBool8(self->getUIViewSize(outWidth, outHeight));
		if (result != 0) {
			SYMBIOSIS_ASSERT(*outWidth > 0);
			SYMBIOSIS_ASSERT(*outHeight > 0);
		}
		return result;
	}
	catch (...) {
		self->recatch();
		return 0;
	}
}

Bool8 SYMBIOSIS_CALL PlugIn::openUIViewThunk(::SymbiosisPlugIn* plugIn, void* nativeGUIElement) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	SYMBIOSIS_ASSERT(nativeGUIElement != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		return toBool8(self->openUIView(nativeGUIElement));
	}
	catch (...) {
		self->recatch();
		return 0;
	}
}

void SYMBIOSIS_CALL PlugIn::closeUIViewThunk(::SymbiosisPlugIn* plugIn) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		self->closeUIView();
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
	}
}

const void* SYMBIOSIS_CALL PlugIn::queryExtensionThunk(::SymbiosisPlugIn* plugIn, UInt32 vendorId, UInt32 interfaceId) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		return self->queryExtension(vendorId, interfaceId);
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
		return 0;
	}
}

void SYMBIOSIS_CALL PlugIn::getLastErrorTextThunk(::SymbiosisPlugIn* plugIn, UInt32 maxLength, UTF8Z* outText) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	SYMBIOSIS_ASSERT(outText != 0);
	SYMBIOSIS_ASSERT(maxLength > 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		self->getLastErrorText(maxLength, outText);
		SYMBIOSIS_ASSERT(isNullTerminatedWithin(outText, maxLength));
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
		self->CommonBase::getLastError(maxLength, outText);
		SYMBIOSIS_ASSERT(isNullTerminatedWithin(outText, maxLength));
	}
}

void SYMBIOSIS_CALL PlugIn::destroyThunk(::SymbiosisPlugIn* plugIn) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		delete self;
	}
	catch (...) {
		SYMBIOSIS_ASSERT(0);
	}
}

void SYMBIOSIS_CALL PlugIn::configureAudioThunk(::SymbiosisPlugIn* plugIn, const SymbiosisConfigureAudioInputArgs* inArgs, SymbiosisConfigureAudioOutputArgs* outArgs) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	SYMBIOSIS_ASSERT(inArgs != 0);
	SYMBIOSIS_ASSERT(outArgs != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		self->configureAudio(inArgs, outArgs);
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
	}
}

void SYMBIOSIS_CALL PlugIn::enableAudioThunk(::SymbiosisPlugIn* plugIn) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		self->enableAudio();
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
	}
}

void SYMBIOSIS_CALL PlugIn::renderAudioThunk(::SymbiosisPlugIn* plugIn, const SymbiosisRenderInputArgs* inArgs, SymbiosisRenderOutputArgs* outArgs) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	SYMBIOSIS_ASSERT(inArgs != 0);
	SYMBIOSIS_ASSERT(outArgs != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		self->renderAudio(inArgs, outArgs);
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
	}
}

void SYMBIOSIS_CALL PlugIn::disableAudioThunk(::SymbiosisPlugIn* plugIn) {
	SYMBIOSIS_ASSERT(plugIn != 0);
	PlugIn* self = reinterpret_cast<PlugIn*>(plugIn);
	try {
		self->disableAudio();
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
	}
}

PlugIn::~PlugIn() {}

const ::SymbiosisPlugInInterface PlugIn::abiInterface = {
	1,
	&PlugIn::changeProgramThunk,
	&PlugIn::updateParameterThunk,
	&PlugIn::getParameterThunk,
	&PlugIn::setBypassThunk,
	&PlugIn::configureAudioThunk,
	&PlugIn::enableAudioThunk,
	&PlugIn::renderAudioThunk,
	&PlugIn::disableAudioThunk,
	&PlugIn::setProgramNameThunk,
	&PlugIn::getProgramNameThunk,
	&PlugIn::convertParameterValueToTextThunk,
	&PlugIn::convertTextToParameterValueThunk,
	&PlugIn::loadStateThunk,
	&PlugIn::createSaveStateThunk,
	&PlugIn::loadProgramStateThunk,
	&PlugIn::createProgramSaveStateThunk,
	&PlugIn::destroySaveStateThunk,
	&PlugIn::getUIViewSizeThunk,
	&PlugIn::openUIViewThunk,
	&PlugIn::closeUIViewThunk,
	&PlugIn::queryExtensionThunk,
	&PlugIn::getLastErrorTextThunk,
	&PlugIn::destroyThunk
};

const void* Factory::queryExtension(UInt32 vendorId, UInt32 interfaceId) {
	(void)vendorId;
	(void)interfaceId;
	return 0;
}

void Factory::getLastErrorText(UInt32 maxLength, UTF8Z* outText) {
	CommonBase::getLastError(maxLength, outText);
}

UInt32 SYMBIOSIS_CALL Factory::getPlugInCountThunk(::SymbiosisFactory* factory) {
	SYMBIOSIS_ASSERT(factory != 0);
	Factory* self = reinterpret_cast<Factory*>(factory);
	try {
		return self->getPlugInCount();
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
		return 0;
	}
}

const SymbiosisPlugInInfo* SYMBIOSIS_CALL Factory::getPlugInInfoThunk(::SymbiosisFactory* factory, UInt32 index) {
	SYMBIOSIS_ASSERT(factory != 0);
	Factory* self = reinterpret_cast<Factory*>(factory);
	try {
		return self->getPlugInInfo(index);
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
		return 0;
	}
}

::SymbiosisPlugIn* SYMBIOSIS_CALL Factory::createPlugInThunk(::SymbiosisFactory* factory, UInt32 index
		, ::SymbiosisHost* host, const ::SymbiosisHostInterface* hostInterface, const ::SymbiosisPlugInInterface** outPlugInInterface) {
	SYMBIOSIS_ASSERT(factory != 0);
	SYMBIOSIS_ASSERT(host != 0);
	SYMBIOSIS_ASSERT(hostInterface != 0);
	SYMBIOSIS_ASSERT(outPlugInInterface != 0);
	Factory* self = reinterpret_cast<Factory*>(factory);
	try {
		std::unique_ptr<HostInterface> hostInterfaceOwner(new PlugIn::CHostInterface(host, hostInterface));
		PlugIn* plugIn = self->createPlugIn(index, hostInterfaceOwner.get());
		if (plugIn == 0) {
			*outPlugInInterface = 0;
			return 0;
		}
		plugIn->adoptHostInterface(std::move(hostInterfaceOwner));
		*outPlugInInterface = plugIn->getABIInterface();
		return plugIn->getABIInstance();
	}
	catch (...) {
		self->recatch();
		*outPlugInInterface = 0;
		return 0;
	}
}

const void* SYMBIOSIS_CALL Factory::queryExtensionThunk(::SymbiosisFactory* factory, UInt32 vendorId, UInt32 interfaceId) {
	SYMBIOSIS_ASSERT(factory != 0);
	Factory* self = reinterpret_cast<Factory*>(factory);
	try {
		return self->queryExtension(vendorId, interfaceId);
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
		return 0;
	}
}

void SYMBIOSIS_CALL Factory::getLastErrorTextThunk(::SymbiosisFactory* factory, UInt32 maxLength, UTF8Z* outText) {
	SYMBIOSIS_ASSERT(factory != 0);
	SYMBIOSIS_ASSERT(outText != 0);
	SYMBIOSIS_ASSERT(maxLength > 0);
	Factory* self = reinterpret_cast<Factory*>(factory);
	try {
		self->getLastErrorText(maxLength, outText);
	}
	catch (...) {
		self->recatch();
		SYMBIOSIS_ASSERT(0);
		self->CommonBase::getLastError(maxLength, outText);
	}
}

void SYMBIOSIS_CALL Factory::destroyThunk(::SymbiosisFactory* factory) {
	SYMBIOSIS_ASSERT(factory != 0);
	Factory* self = reinterpret_cast<Factory*>(factory);
	try {
		delete self;
	}
	catch (...) {
		SYMBIOSIS_ASSERT(0);
	}
}

Factory::~Factory() {}

const ::SymbiosisFactoryInterface Factory::abiInterface = {
	1,
	&Factory::getPlugInCountThunk,
	&Factory::getPlugInInfoThunk,
	&Factory::createPlugInThunk,
	&Factory::queryExtensionThunk,
	&Factory::getLastErrorTextThunk,
	&Factory::destroyThunk
};

::SymbiosisFactory* Factory::bridgeToCFactory(const ::SymbiosisFactoryInterface** outInterface) {
	SYMBIOSIS_ASSERT(outInterface != 0);
	*outInterface = &Factory::abiInterface;
	return reinterpret_cast<::SymbiosisFactory*>(this);
}

void Host::updateDisplay() {}

void Host::beginEdit(UInt32 parameterNumber) {
	(void)parameterNumber;
}

void Host::writeParameter(UInt32 parameterNumber, Float32 normalizedValue) {
	(void)parameterNumber;
	(void)normalizedValue;
}

void Host::endEdit(UInt32 parameterNumber) {
	(void)parameterNumber;
}

bool Host::requestResize(UInt32 width, UInt32 height) {
	(void)width;
	(void)height;
	return false;
}

const void* Host::queryExtension(UInt32 vendorId, UInt32 interfaceId) {
	(void)vendorId;
	(void)interfaceId;
	return 0;
}

void SYMBIOSIS_CALL Host::updateDisplayThunk(::SymbiosisHost* host) {
	SYMBIOSIS_ASSERT(host != 0);
	try {
		reinterpret_cast<Host*>(host)->updateDisplay();
	}
	catch (...) {
		SYMBIOSIS_ASSERT(0);
	}
}

void SYMBIOSIS_CALL Host::beginEditThunk(::SymbiosisHost* host, UInt32 parameterNumber) {
	SYMBIOSIS_ASSERT(host != 0);
	try {
		reinterpret_cast<Host*>(host)->beginEdit(parameterNumber);
	}
	catch (...) {
		SYMBIOSIS_ASSERT(0);
	}
}

void SYMBIOSIS_CALL Host::writeParameterThunk(::SymbiosisHost* host, UInt32 parameterNumber, Float32 normalizedValue) {
	SYMBIOSIS_ASSERT(host != 0);
	SYMBIOSIS_ASSERT(isNormalizedValue(normalizedValue));
	try {
		reinterpret_cast<Host*>(host)->writeParameter(parameterNumber, normalizedValue);
	}
	catch (...) {
		SYMBIOSIS_ASSERT(0);
	}
}

void SYMBIOSIS_CALL Host::endEditThunk(::SymbiosisHost* host, UInt32 parameterNumber) {
	SYMBIOSIS_ASSERT(host != 0);
	try {
		reinterpret_cast<Host*>(host)->endEdit(parameterNumber);
	}
	catch (...) {
		SYMBIOSIS_ASSERT(0);
	}
}

Bool8 SYMBIOSIS_CALL Host::requestResizeThunk(::SymbiosisHost* host, UInt32 width, UInt32 height) {
	SYMBIOSIS_ASSERT(host != 0);
	try {
		const Bool8 result = reinterpret_cast<Host*>(host)->requestResize(width, height) ? 1 : 0;
		SYMBIOSIS_ASSERT(isCanonicalBool8(result));
		return result;
	}
	catch (...) {
		SYMBIOSIS_ASSERT(0);
		return 0;
	}
}

const void* SYMBIOSIS_CALL Host::queryExtensionThunk(::SymbiosisHost* host, UInt32 vendorId, UInt32 interfaceId) {
	SYMBIOSIS_ASSERT(host != 0);
	try {
		return reinterpret_cast<Host*>(host)->queryExtension(vendorId, interfaceId);
	}
	catch (...) {
		SYMBIOSIS_ASSERT(0);
		return 0;
	}
}

Host::~Host() {}

const ::SymbiosisHostInterface Host::abiInterface = {
	1,
	&Host::updateDisplayThunk,
	&Host::beginEditThunk,
	&Host::writeParameterThunk,
	&Host::endEditThunk,
	&Host::requestResizeThunk,
	&Host::queryExtensionThunk
};

HostedPlugIn::HostedPlugIn(::SymbiosisPlugIn* inInstance, const ::SymbiosisPlugInInterface* inApi)
	: instance(inInstance), api(inApi) {}

void HostedPlugIn::changeProgram(UInt32 programNumber) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	api->changeProgram(instance, programNumber);
}

void HostedPlugIn::updateParameter(UInt32 parameterNumber, Float32 normalizedValue) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	api->updateParameter(instance, parameterNumber, normalizedValue);
}

Float32 HostedPlugIn::getParameter(UInt32 parameterNumber) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	return api->getParameter(instance, parameterNumber);
}

void HostedPlugIn::setBypass(bool bypassed) {
	api->setBypass(instance, bypassed ? 1 : 0);
}

void HostedPlugIn::setProgramName(UInt32 programNumber, const UTF8Z* text) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(text != 0);
	api->setProgramName(instance, programNumber, text);
}

void HostedPlugIn::getProgramName(UInt32 programNumber, UInt32 maxLength, UTF8Z* outText) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(outText != 0);
	SYMBIOSIS_ASSERT(maxLength > 0);
	api->getProgramName(instance, programNumber, maxLength, outText);
}

bool HostedPlugIn::convertTextToParameterValue(UInt32 parameterNumber, const UTF8Z* text, Float32* normalizedValue) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(text != 0);
	SYMBIOSIS_ASSERT(normalizedValue != 0);
	return api->convertTextToParameterValue(instance, parameterNumber, text, normalizedValue) != 0;
}

bool HostedPlugIn::convertParameterValueToText(UInt32 parameterNumber, Float32 normalizedValue, UInt32 maxLength, UTF8Z* outText) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(outText != 0);
	SYMBIOSIS_ASSERT(maxLength > 0);
	return api->convertParameterValueToText(instance, parameterNumber, normalizedValue, maxLength, outText) != 0;
}

bool HostedPlugIn::loadState(UInt32 dataSize, const UByte8* data) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(dataSize == 0 || data != 0);
	return api->loadState(instance, dataSize, data) != 0;
}

bool HostedPlugIn::createSaveState(UInt32* dataSize, UByte8** data) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(dataSize != 0);
	SYMBIOSIS_ASSERT(data != 0);
	const bool result = api->createSaveState(instance, dataSize, data) != 0;
	if (result) {
		SYMBIOSIS_ASSERT(*dataSize == 0 || *data != 0);
	}
	return result;
}

bool HostedPlugIn::loadProgramState(UInt32 programNumber, UInt32 dataSize, const UByte8* data) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(dataSize == 0 || data != 0);
	return api->loadProgramState(instance, programNumber, dataSize, data) != 0;
}

bool HostedPlugIn::createProgramSaveState(UInt32 programNumber, UInt32* dataSize, UByte8** data) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(dataSize != 0);
	SYMBIOSIS_ASSERT(data != 0);
	const bool result = api->createProgramSaveState(instance, programNumber, dataSize, data) != 0;
	if (result) {
		SYMBIOSIS_ASSERT(*dataSize == 0 || *data != 0);
	}
	return result;
}

void HostedPlugIn::destroySaveState(UByte8* data) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	api->destroySaveState(instance, data);
}

bool HostedPlugIn::getUIViewSize(UInt32* outWidth, UInt32* outHeight) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(outWidth != 0);
	SYMBIOSIS_ASSERT(outHeight != 0);
	return api->getUIViewSize(instance, outWidth, outHeight) != 0;
}

bool HostedPlugIn::openUIView(void* nativeGUIElement) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(nativeGUIElement != 0);
	return api->openUIView(instance, nativeGUIElement) != 0;
}

void HostedPlugIn::closeUIView() {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	api->closeUIView(instance);
}

const void* HostedPlugIn::queryExtension(UInt32 vendorId, UInt32 interfaceId) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	return api->queryExtension(instance, vendorId, interfaceId);
}

void HostedPlugIn::getLastErrorText(UInt32 maxLength, UTF8Z* outText) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(outText != 0);
	SYMBIOSIS_ASSERT(maxLength > 0);
	api->getLastErrorText(instance, maxLength, outText);
}

void HostedPlugIn::configureAudio(const SymbiosisConfigureAudioInputArgs* inArgs, SymbiosisConfigureAudioOutputArgs* outArgs) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(inArgs != 0);
	SYMBIOSIS_ASSERT(outArgs != 0);
	api->configureAudio(instance, inArgs, outArgs);
}

void HostedPlugIn::enableAudio() {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	api->enableAudio(instance);
}

void HostedPlugIn::renderAudio(const SymbiosisRenderInputArgs* inArgs, SymbiosisRenderOutputArgs* outArgs) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(inArgs != 0);
	SYMBIOSIS_ASSERT(outArgs != 0);
	api->renderAudio(instance, inArgs, outArgs);
}

void HostedPlugIn::disableAudio() {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	api->disableAudio(instance);
}

HostedPlugIn::~HostedPlugIn() {
	if (instance != 0 && api != 0) {
		api->destroy(instance);
		instance = 0;
		api = 0;
	}
}

HostedFactory::HostedFactory(::SymbiosisFactory* inInstance, const ::SymbiosisFactoryInterface* inApi)
	: instance(inInstance), api(inApi) {}

UInt32 HostedFactory::getPlugInCount() {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	return api->getPlugInCount(instance);
}

const SymbiosisPlugInInfo* HostedFactory::getPlugInInfo(UInt32 index) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	return api->getPlugInInfo(instance, index);
}

HostedPlugIn* HostedFactory::createPlugIn(UInt32 index, HostInterface* host) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(host != 0);
	const ::SymbiosisPlugInInterface* plugInApi = 0;
	::SymbiosisPlugIn* plugInInstance = api->createPlugIn(instance, index, reinterpret_cast<::SymbiosisHost*>(host)
			, &Host::abiInterface, &plugInApi);
	if (plugInInstance == 0 || plugInApi == 0) {
		return 0;
	}
	return new HostedPlugIn(plugInInstance, plugInApi);
}

const void* HostedFactory::queryExtension(UInt32 vendorId, UInt32 interfaceId) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	return api->queryExtension(instance, vendorId, interfaceId);
}

void HostedFactory::getLastErrorText(UInt32 maxLength, UTF8Z* outText) {
	SYMBIOSIS_ASSERT(instance != 0);
	SYMBIOSIS_ASSERT(api != 0);
	SYMBIOSIS_ASSERT(outText != 0);
	SYMBIOSIS_ASSERT(maxLength > 0);
	api->getLastErrorText(instance, maxLength, outText);
}

HostedFactory::~HostedFactory() {
	if (instance != 0 && api != 0) {
		api->destroy(instance);
		instance = 0;
		api = 0;
	}
}

} // namespace symbiosis
