/**
	\file PikaScriptImpl.h
	
	PikaScriptImpl contains the template definitions for PikaScript.
	
	You only need to include this file if you want to instantiate a customization on the reference implementation on
	PikaScript. If you are satisfied with the reference implementation (StdScript), you only need to include
	PikaScript.h and add PikaScript.cpp to your project.
	
	\version
	
	Version 0.97
	
	\page Copyright
	
	PikaScript is released under the BSD 2-Clause License. https://opensource.org/licenses/BSD-2-Clause
	
	Copyright (c) 2008-2025, NuEdge Development / Magnus Lidstroem
	All rights reserved.
	
	Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
	following conditions are met:
	
	Redistributions of source code must retain the above copyright notice, this list of conditions and the following
	disclaimer.
	
	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
	disclaimer in the documentation and/or other materials provided with the distribution.
	
	
	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
	INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
	SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
	WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef PikaScriptImpl_h
#define PikaScriptImpl_h

#include <math.h>
#include <time.h>
#include <string.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <fstream>
#include <limits>
#include <locale>
#if !defined(PikaScript_h)
#include "PikaScript.h"
#endif

namespace Pika {

template<typename T> inline T mini(T a, T b) { return (a < b) ? a : b; }
template<typename T> inline T maxi(T a, T b) { return (a < b) ? b : a; }

// Usually I am pretty militant against macros, but sorry, the following ones are just too handy.

// FIX : is there *some* way to solve this without ugly macros?
#if (PIKA_UNICODE)
	#define STR(s) L##s
#else
	#define STR(x) x
#endif
#define TMPL template<class CFG>
#define T_TYPE(x) typename Script<CFG>::x

/* --- Utility Routines --- */

inline ushort ushortChar(char c) { return uchar(c); }
inline ushort ushortChar(wchar_t c) { return ushort(c); }
inline ulong ulongChar(char c) { return static_cast<uchar>(c); }
inline ulong ulongChar(wchar_t c) { return static_cast<ulong>(c); }
template<class C> std::basic_ostream<C>& xcout();
template<class C> std::basic_istream<C>& xcin();
template<> inline std::basic_ostream<char>& xcout() { return std::cout; }
template<> inline std::basic_ostream<wchar_t>& xcout() { return std::wcout; }
template<> inline std::basic_istream<char>& xcin() { return std::cin; }
template<> inline std::basic_istream<wchar_t>& xcin() { return std::wcin; }
template<> inline std::string toStdString(const std::string& s) { return s; }

inline ulong shiftRight(ulong l, int r) { return l >> r; }
inline ulong shiftLeft(ulong l, int r) { return l << r; }
inline ulong bitAnd(ulong l, ulong r) { return l & r; }
inline ulong bitOr(ulong l, ulong r) { return l | r; }
inline ulong bitXor(ulong l, ulong r) { return l ^ r; }
inline double modulo(double x, double y) { return fmod(x, y); }

template<class C> inline bool isSymbolChar(C c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

template<class C> inline bool maybeWhite(C c) { return (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '/'); }

template<class S> std::string toStdString(const S& s) { return std::string(s.begin(), s.end()); }
template<> std::string toStdString(const std::string& s);
inline std::string& toStdString(std::string& s) { return s; }

template<class S> ulong hexToLong(typename S::const_iterator& p, const typename S::const_iterator& e) {
	assert(p <= e);
	ulong l = 0;
	for (; p < e && ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'F') || (*p >= 'a' && *p <= 'f')); ++p)
		l = (l << 4) + (*p <= '9' ? *p - '0' : (*p & ~0x20) - ('A' - 10));
	return l;
}

template<class S> long stringToLong(typename S::const_iterator& p, const typename S::const_iterator& e) {
	assert(p <= e);
	bool negative = (e - p > 1 && ((*p == '+' || *p == '-') && p[1] >= '0' && p[1] <= '9') ? (*p++ == '-') : false);
	long l = 0;
	for (; p < e && *p >= '0' && *p <= '9'; ++p) l = l * 10 + (*p - '0');
	return negative ? -l : l;
}

template<class S, class T> S intToString(T i, int radix, int minLength) {
	assert(2 <= radix && radix <= 16);
	assert(0 <= minLength && minLength <= static_cast<int>(sizeof (T) * 8));
	typename S::value_type buffer[sizeof (T) * 8 + 1], * p = buffer + sizeof (T) * 8 + 1, * e = p - minLength;
	for (T x = i; p > e || x != 0; x /= radix) {
		assert(p >= buffer + 2);
		*--p = STR("fedcba9876543210123456789abcdef")[15 + x % radix];													// Mirrored hex string to handle negative x.
	}
	if (std::numeric_limits<T>::is_signed && i < 0) *--p = '-';
	return S(p, buffer + sizeof (T) * 8 + 1 - p);
}

template<class S> double stringToDouble(typename S::const_iterator& p, const typename S::const_iterator& e) {
	assert(p <= e);
	double d = 0, sign = (e - p > 1 && (*p == '+' || *p == '-') ? (*p++ == '-' ? -1.0 : 1.0) : 1.0);
	if (std::numeric_limits<double>::has_infinity && e - p >= 8 && equal(p, p + 8, STR("infinity"))) {
		p += 8;
		d = std::numeric_limits<double>::infinity();
	} else if (p < e && *p >= '0' && *p <= '9') {
		if (*p == '0') ++p; else do { d = d * 10.0 + (*p - '0'); } while (++p < e && *p >= '0' && *p <= '9');
		if (e - p > 1 && *p == '.' && p[1] >= '0' && p[1] <= '9') {
			++p;
			double f = 1.0;
			do { d += (*p - '0') * (f *= 0.1); } while (++p < e && *p >= '0' && *p <= '9');
		}
		if (e - p > 1 && (*p == 'E' || *p == 'e')) {
			typename S::const_iterator b = p;
			d *= pow(10, double(stringToLong<S>(++p, e)));
			if (p == b + 1) p = b;
		}
	}
	return d * sign;
}

template<class S> bool stringToDouble(const S& s, double& d) {
	typename S::const_iterator b = s.begin(), e = s.end(), p = b;
	d = stringToDouble<S>(p, e);
	return (p != b && p >= e);
}

template<class S> S doubleToString(double d, int precision) {
	assert(1 <= precision && precision <= 24);
	const double EPSILON = 1.0e-300, SMALL = 1.0e-5, LARGE = 1.0e+10;
	double x = fabs(d), y = x;
	if (y <= EPSILON) return S(STR("0"));
	else if (precision >= 12 && y < LARGE && long(d) == d) return intToString<S, long>(long(d));
	else if (std::numeric_limits<double>::has_infinity && x == std::numeric_limits<double>::infinity())
		return d < 0 ? S(STR("-infinity")) : S(STR("+infinity"));
	typename S::value_type buffer[32], * bp = buffer + 2, * dp = bp, * pp = dp + 1, * ep = pp + precision;
	for (; x >= 10.0 && pp < ep; x *= 0.1) ++pp;																		// Normalize values > 10 and move period position.
	if (pp >= ep || y <= SMALL || y >= LARGE) {																			// Exponential treatment of very small or large values.
		double e = floor(log10(y) + 1.0e-10);
		S exps(e >= 0 ? S(STR("e+")) : S(STR("e")));
		exps += intToString<S, int>(int(e));
		int maxp = 15;																									// Limit precision because of rounding errors in log10 etc
		for (double f = fabs(e); f >= 8; f /= 10) --maxp;
		return (doubleToString<S>(d * pow(0.1, e), mini(maxp, precision)) += exps);
	}
	for (; x < 1.0 && dp < buffer + 32; ++ep, x *= 10.0) {																// For values < 1, spit out leading 0's and increase precision.
		*dp++ = '0';
		if (dp == pp) *dp++ = '9';																						// Hop over period position (set to 9 to avoid when eliminating 9's).
	}
	for (; dp < ep; ) {																									// Exhaust all remaining digits of mantissa into buffer.
		uint ix = uint(x);
		*dp++ = ix + '0';
		if (dp == pp) *dp++ = '9';																						// Hop over period position (set to 9 to avoid when eliminating 9's).
		x = (x - ix) * 10.0;
	}
	if (x >= 5) {																										// If remainder is >= 5, increment trailing 9's...
		while (dp[-1] == '9') *--dp = '0';
		if (dp == bp) *--bp = '1'; else dp[-1]++;																		// If we are at spare position, set to '1' and include, otherwise, increment last non-9.
	}
	*pp = '.';
	if (ep > pp) while (ep[-1] == '0') --ep;
	if (ep - 1 == pp) --ep;
	if (d < 0) *--bp = '-';
	return S(bp, ep - bp);
}

const int ESCAPE_CODE_COUNT = 10;

template<class S> S unescape(typename S::const_iterator& p, const typename S::const_iterator& e) {
	assert(p <= e);
	typedef typename S::value_type CHAR;
	static const CHAR ESCAPE_CHARS[ESCAPE_CODE_COUNT] = { '\\', '\"', '\'',  'a',  'b',  'f',  'n',  'r',  't',  'v' };
	static const CHAR ESCAPE_CODES[ESCAPE_CODE_COUNT] = { '\\', '\"', '\'', '\a', '\b', '\f', '\n', '\r', '\t', '\v' };
	if (p >= e || (*p != '"' && *p != '\'')) throw Exception<S>(STR("Invalid string literal"));
	S d;
	typename S::const_iterator b = ++p;
	if (p[-1] == '\'') while (e - (p = std::find(p, e, '\'')) > 1 && p[1] == '\'') { d += S(b, ++p); b = ++p; }
	else while (p < e && *p != '\"') {
		if (*p == '\\' && e - p > 1) {
			d += S(b, p);
			const CHAR* f = std::find(ESCAPE_CHARS, ESCAPE_CHARS + ESCAPE_CODE_COUNT, *++p);
			ulong l;
			if (f != ESCAPE_CHARS + ESCAPE_CODE_COUNT) { ++p; l = ESCAPE_CODES[f - ESCAPE_CHARS]; }
			else if (*p == 'x') { b = ++p; l = hexToLong<S>(p, (e - p > 2 ? p + 2 : e)); }
			else if (*p == 'u') { b = ++p; l = hexToLong<S>(p, (e - p > 4 ? p + 4 : e)); }
			else if (*p == 'U') { b = ++p; l = hexToLong<S>(p, (e - p > 8 ? p + 8 : e)); }
			else { b = p; l = stringToLong<S>(p, e); }
			if (p == b) throw Exception<S>(STR("Invalid escape character"));
			b = p;
			d += CHAR(l);
		} else ++p;
	}
	if (p >= e) throw Exception<S>(STR("Unterminated string"));
	return (d += S(b, p++));
}

template<class S> S escape(const S& s) {
	typedef typename S::value_type CHAR;
	static const CHAR ESCAPE_CHARS[ESCAPE_CODE_COUNT] = { '\\', '\"', '\'',  'a',  'b',  'f',  'n',  'r',  't',  'v' };
	static const CHAR ESCAPE_CODES[ESCAPE_CODE_COUNT] = { '\\', '\"', '\'', '\a', '\b', '\f', '\n', '\r', '\t', '\v' };
	typename S::const_iterator b = s.begin(), e = s.end();
	bool needToBackUp = false;																								// If we need to re-escape with " " and string contained " or \ we need to start over from the beginning.
	for (; b < e && *b >= 32 && *b <= 126 && *b != '\''; ++b) needToBackUp = needToBackUp || (*b == '\\' || *b == '\"');
	if (b >= e) return ((S(STR("'")) += s) += '\'');
	if (needToBackUp) b = s.begin();
	typename S::const_iterator l = s.begin();
	S d = S(STR("\""));
	while (true) {
		while (b < e && *b >= 32 && *b <= 126 && *b != '\\' && *b != '\"') ++b;
		d += S(l, b);
		if (b >= e) break;
		const CHAR* f = std::find(ESCAPE_CODES, ESCAPE_CODES + ESCAPE_CODE_COUNT, *b);
		if (f != ESCAPE_CODES + ESCAPE_CODE_COUNT) (d += '\\') += ESCAPE_CHARS[f - ESCAPE_CODES];
		else if (uchar(*b) == ushortChar(*b)) (d += STR("\\x")) += intToString<S>(uchar(*b), 16, 2);
		else if (ushortChar(*b) == ulongChar(*b)) (d += STR("\\u")) += intToString<S>(ushortChar(*b), 16, 4);
		else (d += STR("\\U")) += intToString<S>(ulongChar(*b), 16, 8);
		l = ++b;
	}
	return (d += '\"');
}

/* --- STLValue --- */

template<class S> STLValue<S>::operator bool() const {
	if (S(*this) == STR("false")) return false;
	else if (S(*this) == STR("true")) return true;
	else throw Exception<S>(S(STR("Invalid boolean: ")) += escape(S(*this)));
}

template<class S> STLValue<S>::operator long() const {
	typename S::const_iterator p = S::begin();
	long y = stringToLong<S>(p, S::end());
	if (p == S::begin() || p < S::end()) throw Exception<S>(S(STR("Invalid integer: ")) += escape(S(*this)));
	return y;
}

template<class S> STLValue<S>::operator double() const {
	double d;
	if (!stringToDouble(*this, d)) throw Exception<S>(S(STR("Invalid number: ")) += escape(S(*this)));
	return d;
}

template<class S> bool STLValue<S>::operator<(const STLValue& r) const {
	double lv, rv;
	bool lnum = stringToDouble(*this, lv), rnum = stringToDouble(r, rv);
	return (lnum == rnum ? (lnum ? lv < rv : (const S&)(*this) < (const S&)(r)) : lnum);
}

template<class S> bool STLValue<S>::operator==(const STLValue& r) const {
	double lv, rv;
	bool lnum = stringToDouble(*this, lv), rnum = stringToDouble(r, rv);
	return (lnum == rnum && (lnum ? lv == rv : (const S&)(*this) == (const S&)(r)));
}

template<class S> const STLValue<S> STLValue<S>::operator[](const STLValue& i) const {
	typename S::const_iterator b = S::begin(), p = S::end();
	if (p > b) switch (*(p - 1)) {
		case '$':	if (--p == b) break; /* else continue */
		case '^':	while (p > b && *(p - 1) == '^') --p; if (p == b) break; /* else continue */
		case ':':	if (p - 1 > b && *(p - 1) == ':' && *b == ':' && std::find(b + 1, p, ':') == p - 1) p = b; break;
	}
	return (p == b) ? S(*this) + S(i) : (S(*this) + STR('.')) += S(i);
}

/* --- Frame --- */

TMPL Script<CFG>::Frame::Frame(Variables& vars, Root& root, Frame* previous) : vars(vars), root(root)
		, previous(previous), closure(this), label(previous == 0 ? String(STR("::")) : root.generateLabel()) { }

TMPL T_TYPE(Value) Script<CFG>::Frame::rvalue(const XValue& v, bool fallback) {
	return !v.first ? v.second : get(v.second, fallback);
}

TMPL const T_TYPE(Value)& Script<CFG>::Frame::lvalue(const XValue& v) {
	if (!v.first) throw Xception(STR("Invalid lvalue"));
	return v.second;
}

TMPL T_TYPE(Frame*) Script<CFG>::Frame::resolveFrame(StringIt& p, const StringIt& e) const {
	assert(p <= e);
	Frame* f = const_cast<Frame*>(this);
	if (p < e && *p == ':') {
		StringIt n = std::find(p + 1, e, ':');
		if (n >= e) throw Xception(String(STR("Invalid identifier: ")) += escape(String(p, e)));
		if (n - p > 1) {
			String s(p, n + 1);
			while (f->label != s)
				if ((f = f->previous) == 0) throw Xception(String(STR("Frame does not exist: ")) += escape(s));
		} else f = &root;
		p = n + 1;
	}
	for (; p < e && *p == '^'; ++p)
		if ((f = f->previous) == 0) throw Xception(STR("Frame does not exist"));
	if (p >= e || *p != '$') f = f->closure;
	return f;
}

TMPL std::pair< T_TYPE(Frame*), T_TYPE(String) > Script<CFG>::Frame::resolveFrame(const String& identifier) const {
	switch (identifier[0]) {
		default:	return std::pair<Frame*, String>(closure, identifier);
		case '$':	return std::pair<Frame*, String>(const_cast<Frame*>(this), identifier);
		case ':': case '^': {
			StringIt b = const_cast<const String&>(identifier).begin(), e = const_cast<const String&>(identifier).end();
			Frame* frame = resolveFrame(b, e);
			return std::pair<Frame*, String>(frame, String(b, e));
		}
	}
}

TMPL T_TYPE(Value) Script<CFG>::Frame::get(const String& identifier, bool fallback) const {
	Value temp;
	std::pair<Frame*, String> fs = resolveFrame(identifier);
	if (fs.first->vars.lookup(fs.second, temp)) return temp;
	else if (fallback && identifier[0] != '$' && isSymbolChar(identifier[0]) && root.vars.lookup(fs.second, temp))
		return temp;
	else throw Xception(String(STR("Undefined: ")) += escape(identifier));
}

TMPL T_TYPE(Value) Script<CFG>::Frame::getOptional(const String& identifier, const Value& defaultValue) const {
	std::pair<Frame*, String> fs = resolveFrame(identifier);
	Value temp;
	return fs.first->vars.lookup(fs.second, temp) ? temp : defaultValue;
}

TMPL const T_TYPE(Value)& Script<CFG>::Frame::set(const String& identifier, const Value& v) {
	std::pair<Frame*, String> fs = resolveFrame(identifier);
	if (!fs.first->vars.assign(fs.second, v)) throw Xception(String(STR("Cannot modify: ")) += escape(identifier));
	return v;
}

TMPL T_TYPE(Value) Script<CFG>::Frame::reference(const String& identifier) const {
	std::pair<Frame*, String> fs = resolveFrame(identifier);
	return fs.first->label + fs.second;
}

TMPL T_TYPE(Value) Script<CFG>::Frame::execute(const String& body) {
	StringIt b = body.begin(), e = body.end();
	switch (b < e ? *b : 0) {
		case '{':	return evaluate(body);																				// <-- function
		case '>':	closure = resolveFrame(++b, e);																		// <-- lambda
					return evaluate(String(b, e));
		case '<':	if (--e - ++b > 0) {																				// <-- native
						Frame* nativeFrame = (*b == ':' ? resolveFrame(b, e) : &root);
						Native* native = nativeFrame->vars.lookupNative(String(b, e));
						if (native != 0) return native->pikaCall(*this);
					}
					throw Xception(String(STR("Unknown native function: ")) += escape(body));
		default:	throw Xception(String(STR("Illegal call on: ")) + escape(body));
	}
}

TMPL void Script<CFG>::Frame::white(StringIt& p, const StringIt& e) {
	assert(p <= e);
	while (p < e) {
		switch (*p) {
			case ' ': case '\t': case '\r': case '\n': ++p; break;
			case '/':	if (e - p > 1 && p[1] == '/') {
							static const Char END_CHARS[] = { '\r', '\n' };
							p = find_first_of(p += 2, e, END_CHARS, END_CHARS + 2);
							break;
						} else if (e - p > 1 && p[1] == '*') {
							static const Char END_CHARS[] = { '*', '/' };
							p = search(p += 2, e, END_CHARS, END_CHARS + 2);
							if (p >= e) throw Xception(STR("Missing '*/'"));
							p += 2;
							break;
						} /* else continue */
			default:	return;
		}
	}
}

TMPL bool Script<CFG>::Frame::token(StringIt& p, const StringIt& e, const Char* token) {
	assert(p <= e);
	StringIt t = p + 1;
	while (*token != 0 && t < e && *t == *token) { ++t; ++token; }
	if (*token == 0 && (t >= e || !isSymbolChar(*t))) {
		if ((p = t) < e && maybeWhite(*p)) white(p, e);
		return true;
	} else return false;
}

TMPL template<class F> bool Script<CFG>::Frame::binaryOp(StringIt& p, const StringIt& e, XValue& v, bool dry
		, Precedence thres, int hop, Precedence prec, F op) {
	assert(p <= e);
	assert(hop >= 0);
	if (thres >= prec) return false;
	XValue r;
	expr(p += hop, e, r, false, dry, prec);
	if (!dry) v = XValue(false, op(rvalue(v), rvalue(r)));
	return true;
}

TMPL template<class F> bool Script<CFG>::Frame::assignableOp(StringIt& p, const StringIt& e, XValue& v, bool dry
		, Precedence thres, int hop, Precedence prec, F op) {
	assert(p <= e);
	assert(hop >= 0);
	if (p + hop >= e || p[hop] != '=') return binaryOp(p, e, v, dry, thres, hop, prec, op);
	if (thres > ASSIGN) return false;
	XValue r;																											// <-- operate and assign
	expr(p += hop + 1, e, r, false, dry, ASSIGN);
	if (!dry) v = XValue(false, set(lvalue(v), op(rvalue(v, false), rvalue(r))));
	return true;
}

TMPL template<class F> bool Script<CFG>::Frame::addSubOp(StringIt& p, const StringIt& e, XValue& v, bool dry
		, Precedence thres, const F& f) {
	assert(p <= e);
	if (p + 1 >= e || p[1] != *p) return assignableOp(p, e, v, dry, thres, 1, ADD_SUB, f);
	else if (thres >= POSTFIX) return false;
	else if (!dry) {
		Value r = rvalue(v, false);																						// <-- post inc/dec
		set(lvalue(v), f(long(r), 1));
		v = XValue(false, r);
	}
	p += 2;
	return true;
}

TMPL template<class E, class I, class S> bool Script<CFG>::Frame::lgtOp(StringIt& p, const StringIt& e, XValue& v
		, bool dry, Precedence thres, const E& excl, const I& incl, S shift) {
	assert(p <= e);
	if (e - p > 1 && p[1] == *p) return assignableOp(p, e, v, dry, thres, 2, SHIFT, shift);								// <-- shift
	else if (e - p > 1 && p[1] == '=') return binaryOp(p, e, v, dry, thres, 2, COMPARE, incl);							// <-- less/greater or equal
	else return binaryOp(p, e, v, dry, thres, 1, COMPARE, excl);														// <-- less/greater
}

TMPL bool Script<CFG>::Frame::pre(StringIt& p, const StringIt& e, XValue& v, bool dry) {
	assert(p <= e);
	StringIt b = p;
	switch (p < e ? *p : 0) {
		case 0:		return false;
		case '!':	expr(++p, e, v, false, dry, PREFIX); if (!dry) v = XValue(false, !rvalue(v)); return true;			// <-- logical not
		case '~':	expr(++p, e, v, false, dry, PREFIX); if (!dry) v = XValue(false, ~ulong(rvalue(v))); return true;	// <-- bitwise not
		case '(':	termExpr(++p, e, v, false, dry, BRACKETS, ')'); return true;										// <-- parenthesis
		case ':':	if (e - p > 1 && p[1] == ':') p += 2; break;														// <-- root
		case '^':	while (++p < e && *p == '^'); break;																// <-- frame peek
		case '@':	expr(++p, e, v, false, dry, PREFIX); if (!dry) v = XValue(false, reference(lvalue(v))); return true;// <-- reference
		case '[':	termExpr(++p, e, v, false, dry, BRACKETS, ']'); if (!dry) v = XValue(true, rvalue(v)); return true;	// <-- indirection
		case '<':	p = std::find(p, e, '>'); if (p < e) ++p; if (!dry) v = XValue(false, String(b, p)); return true;	// <-- native literal
		case '\'': case '"': { Value s(unescape<String>(p, e)); if (!dry) v = XValue(false, s); } return true;			// <-- string literal
		case 'e':	if (token(p, e, STR("lse"))) throw Xception(STR("Unexpected 'else' (preceded by ';'?)")); break;	// <-- error on unexpected else
		case 't':	if (token(p, e, STR("rue"))) { if (!dry) v = XValue(false, true); return true; } break;				// <-- true literal
		case 'v':	if (token(p, e, STR("oid"))) { if (!dry) v = XValue(false, Value()); return true; } break;			// <-- void literal

		case '>':	if (++p < e && maybeWhite(*p)) white(p, e);															// <-- lambda
					b = p;
					expr(p, e, v, false, true, STATEMENT);
					if (!dry) v = XValue(false, (String(STR(">")) += closure->label) += String(b, p));
					return true;

		case '{':	do { expr(++p, e, v, true, dry, STATEMENT); } while (p < e && *p == ';');							// <-- compound
					if (p >= e) throw Xception(STR("Missing '}'"));
					if (*p != '}') throw Xception(STR("Syntax error (missing ';')?"));
					++p;
					return true;

		case '+': case '-':
					if (token(p, e, STR("infinity"))) p = b + 1; /* and continue to stringToDouble */					// <-- infinity literal
					else if (++p >= e) return false;
					else if (*p == *b) {
						expr(++p, e, v, false, dry, PREFIX);															// <-- pre inc/dec
						if (!dry) v = XValue(false, set(lvalue(v), long(rvalue(v, false)) + (*b == '-' ? -1 : 1)));
						return true;
					} else if (*p < '0' || *p > '9') {
						expr(p, e, v, false, dry, PREFIX);																// <-- positive / negative
						if (!dry) v = XValue(false, *b == '-' ? -double(rvalue(v)) : double(rvalue(v)));
						return true;
					} /* else continue */

		case '0':	if (e - p > 1 && p[1] == 'x') {
						ulong l = hexToLong<String>(p += 2, e);															// <-- hexadecimal literal
						if (p == b + 2) throw Xception(STR("Invalid hexadecimal number"));
						if (!dry) v = XValue(false, *b == '-' ? -long(l) : l);
						return true;
					} /* else continue */

		case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': {						// <-- numeric literal
						double d = stringToDouble<String>(p, e);
						if (!dry) v = XValue(false, *b == '-' ? -d : d);
					}
					return true;

		case 'f': 	if (token(p, e, STR("alse"))) { if (!dry) v = XValue(false, false); return true; }					// <-- false literal
					else if (token(p, e, STR("or"))) {
						if (p >= e || *p != '(') throw Xception(STR("Expected '('"));									// <-- for
						XValue xv;
						termExpr(++p, e, xv, true, dry, ARGUMENT, ';');
						StringIt cp = p;
						termExpr(p, e, xv, true, dry, ARGUMENT, ';');
						StringIt ip = p;
						termExpr(p, e, xv, true, true, ARGUMENT, ')');
						StringIt bp = p;
						bool cb = !dry && rvalue(xv);
						do {
							expr(p = bp, e, v, true, !cb, BODY);
							if (cb) {
								if (root.doTrace(TRACE_LOOP)) tick(p, v, TRACE_LOOP, true);
								StringIt ep = p;
								expr(p = ip, e, xv, true, false, ARGUMENT);
								expr(p = cp, e, xv, true, false, ARGUMENT);
								p = ep;
								cb = rvalue(xv);
							}
						} while (cb);
						if (!dry && root.doTrace(TRACE_LOOP)) tick(p, v, TRACE_LOOP, false);
						return true;
					} else if (token(p, e, STR("unction"))) {
						if (p >= e || *p != '{') throw Xception(STR("Expected '{'"));
						b = p;
						expr(p, e, v, false, true, DEFINITION);															// <-- function
						if (!dry) v = XValue(false, String(b, p));
						return true;
					}
					break;

		case 'i':	if (e - p > 1 && token(p, e, STR("f"))) {
						if (p >= e || *p != '(') throw Xception(STR("Expected '('"));									// <-- if
						XValue c;
						termExpr(++p, e, c, false, dry, ARGUMENT, ')');
						bool b = dry || rvalue(c);
						expr(p, e, v, false, dry || !b, BODY);
						if (p < e && *p == 'e' && token(p, e, STR("lse"))) expr(p, e, v, false, dry || b, BODY);		// <-- else
						return true;
					}
					break;
	}
	while (p < e && isSymbolChar(*p)) ++p;
	if (b != p && !dry) v = XValue(true, String(b, p));
	return (b != p);
}

TMPL long Script<CFG>::Frame::intDiv(long x, long y) {
	if (y == 0) {
		throw Xception(STR("Division by zero"));
	}
	return x / y;
}

TMPL bool Script<CFG>::Frame::post(StringIt& p, const StringIt& e, XValue& v, bool dry, Precedence thres) {
	assert(p <= e);
	switch (p < e ? *p : 0) {
		case 0:		return false;
		case ' ': case '\t': case '\r': case '\n':
					if (thres < DEFINITION) { Char c = *p; while (++p < e && *p == c) { } return true; } break;			// <-- white spaces
		case '/':	if (thres < DEFINITION && e - p > 1 && (p[1] == '/' || p[1] == '*')) { white(p, e); return true; }	// <-- comment
					return assignableOp(p, e, v, dry, thres, 1, MUL_DIV, std::divides<double>());						// <-- divide
		case '+':	return addSubOp(p, e, v, dry, thres, std::plus<double>());											// <-- add
		case '-':	return addSubOp(p, e, v, dry, thres, std::minus<double>());											// <-- subtract
		case '#':	return assignableOp(p, e, v, dry, thres, 1, CONCAT, std::plus<String>());							// <-- concat
		case '*':	return assignableOp(p, e, v, dry, thres, 1, MUL_DIV, std::multiplies<double>());					// <-- multipy
		case '\\':	return assignableOp(p, e, v, dry, thres, 1, MUL_DIV, intDiv);										// <-- integer division
		case '%':	return assignableOp(p, e, v, dry, thres, 1, MUL_DIV, modulo);										// <-- modulus
		case '^':	return assignableOp(p, e, v, dry, thres, 1, BIT_XOR, bitXor);										// <-- xor
		case '<':	return lgtOp(p, e, v, dry, thres, std::less<Value>(), std::less_equal<Value>(), shiftLeft);			// <-- shift left
		case '>':	return lgtOp(p, e, v, dry, thres, std::greater<Value>(), std::greater_equal<Value>(), shiftRight);	// <-- shift right

		case '!':	if (e - p > 2 && p[2] == '=' && p[1] == '=')
						return binaryOp(p, e, v, dry, thres, 3, EQUALITY, std::not_equal_to<String>());					// <-- literal not equals
					else if (e - p > 1 && p[1] == '=')
						return binaryOp(p, e, v, dry, thres, 2, EQUALITY, std::not_equal_to<Value>());					// <-- not equals
					break;

		case '=':	if (e - p > 2 && p[2] == '=' && p[1] == '=')
						return binaryOp(p, e, v, dry, thres, 3, EQUALITY, std::equal_to<String>());						// <-- literal equals
					else if (e - p > 1 && p[1] == '=')
						return binaryOp(p, e, v, dry, thres, 2, EQUALITY, std::equal_to<Value>());						// <-- equals
					else if (thres <= ASSIGN) {
						XValue r;																						// <-- assign
						expr(++p, e, r, false, dry, ASSIGN);
						if (!dry) v = XValue(false, set(lvalue(v), rvalue(r)));
						return true;
					}
					break;

		case '&':	if (e - p < 2 || p[1] != '&') return assignableOp(p, e, v, dry, thres, 1, BIT_AND, bitAnd);			// <-- bitwise and
					else if (thres < LOGICAL_AND) {
						bool l = !dry && rvalue(v);																		// <-- logical and
						expr(p += 2, e, v, false, !l, LOGICAL_AND);
						if (!dry) v = XValue(false, l && rvalue(v));
						return true;
					}
					break;

		case '|': 	if (e - p < 2 || p[1] != '|') return assignableOp(p, e, v, dry, thres, 1, BIT_OR, bitOr);			// <-- bitwise or
					else if (thres < LOGICAL_OR) {
						bool l = dry || rvalue(v);																		// <-- logical or
						expr(p += 2, e, v, false, l, LOGICAL_OR);
						if (!dry) v = XValue(false, l || rvalue(v));
						return true;
					}
					break;

		case '.':	{																									// <-- member
						if (++p < e && maybeWhite(*p)) white(p, e);
						StringIt b = p;
						while (p < e && isSymbolChar(*p)) ++p;
						if (!dry) v = XValue(true, lvalue(v)[String(b, p)]);
						return true;
					}

		case '[':	if (thres < POSTFIX) {																				// <-- subscript
						XValue element;
						termExpr(++p, e, element, false, dry, BRACKETS, ']');
						if (!dry) v = XValue(true, lvalue(v)[rvalue(element)]);
						return true;
					}
					break;

		case '{':	if (thres < POSTFIX) {																				// <-- substring
						XValue index;
						bool gotIndex = expr(++p, e, index, true, dry, BRACKETS);
						if (p >= e || (*p != ':' && *p != '}')) throw Xception(STR("Expected '}' or ':'"));
						if (*p++ == ':') {
							XValue count;
							bool gotCount = termExpr(p, e, count, true, dry, BRACKETS, '}');
							if (!dry) {
								String s = rvalue(v);
								long i = !gotIndex ? 0L : long(rvalue(index));
								long n = gotCount ? long(rvalue(count)) + mini(i, 0L) : String::npos;
								v = XValue(false, i <= long(s.size()) && (!gotCount || n >= 0L)
										? Value(s.substr(maxi(i, 0L), n)) : Value());
							}
						} else if (gotIndex && !dry) {
							String s = rvalue(v);
							long i = long(rvalue(index));
							v = XValue(false, i >= 0L && i <= long(s.size()) ? Value(s.substr(i, 1)) : Value());
						} else if (!dry) throw Xception(STR("Syntax error"));
						return true;
					}
					break;

		case '(': 	if (thres < POSTFIX) {																				// <-- call
						typename CFG::Locals locals;
						Frame calleeFrame(locals, root, this);
						long n = 0;
						do {
							if (++p < e && maybeWhite(*p)) white(p, e);
							if (p < e && *p == ')' && n == 0) break;
							XValue arg;
							if (expr(p, e, arg, true, dry, ARGUMENT) && !dry)
								locals.assign(String(STR("$")) += intToString<String>(n), rvalue(arg));
							++n;
						} while (p < e && *p == ',');
						if (p >= e || *p != ')') throw Xception(STR("Expected ',' or ')'"));
						++p;
						if (!dry) {
							locals.assign(STR("$n"), n);
							if (v.first) locals.assign(STR("$callee"), v.second);
							v = XValue(false, calleeFrame.execute(rvalue(v)));
						}
						return true;
					}
					break;
	}
	return false;
}

TMPL void Script<CFG>::Frame::tick(const StringIt& p, const XValue& v, Precedence thres, bool exit) {
	root.trace(*this, *source, p - source->begin(), v.first, v.second, thres, exit);
}

TMPL bool Script<CFG>::Frame::expr(StringIt& p, const StringIt& e, XValue& v, bool emptyOk, bool dry, Precedence thres) {
	assert(p <= e);
	if (p < e && maybeWhite(*p)) white(p, e);
	if (!dry && root.doTrace(thres)) tick(p, v, thres, false);
	if (pre(p, e, v, dry)) {
		while (post(p, e, v, dry, thres));
		if (!dry && root.doTrace(thres)) tick(p, v, thres, true);
		return true;
	} else if (!emptyOk) throw Xception(STR("Syntax error"));
	return false;
}

TMPL bool Script<CFG>::Frame::termExpr(StringIt& p, const StringIt& e, XValue& v, bool emptyOk, bool dry
		, Precedence thres, Char term) {
	assert(p <= e);
	bool nonEmpty = expr(p, e, v, emptyOk, dry, thres);
	if (p >= e || *p != term) throw Xception((String(STR("Missing '")) += String(&term, 1)) += '\'');
	++p;
	return nonEmpty;
}

TMPL T_TYPE(Value) Script<CFG>::Frame::evaluate(const String source) {
	XValue v;
	const String* oldSource = this->source;
	this->source = &source;
	try {
		StringIt p = source.begin(), e = source.end();
		if (root.doTrace(TRACE_CALL)) tick(p, v, TRACE_CALL, false);
		try {
			try {
				while (p < e) {
					expr(p, e, v, true, false, STATEMENT);
					if (p < e) {
						if (*p != ';') throw Xception(STR("Syntax error"));
						++p;
					}
				}
				v = XValue(false, rvalue(v));
			} catch (const Xception& x) {
				if (root.doTrace(TRACE_ERROR)) tick(p, XValue(false, x.getError()), TRACE_ERROR, previous == 0);
				throw;
			} catch (const std::exception& x) {
				if (root.doTrace(TRACE_ERROR)) {
					const char* s = x.what();
					String err = String(std::basic_string<Char>(s, s + strlen(s)));
					tick(p, XValue(false, err), TRACE_ERROR, previous == 0);
				}
				throw;
			} catch (...) {
				if (root.doTrace(TRACE_ERROR))
					tick(p, XValue(false, STR("Unknown exception")), TRACE_ERROR, previous == 0);
				throw;
			}
		} catch (...) {
			if (root.doTrace(TRACE_CALL)) tick(p, v, TRACE_CALL, true);
			throw;
		}
		if (root.doTrace(TRACE_CALL)) tick(p, v, TRACE_CALL, true);
	} catch (...) {
		this->source = oldSource;
		throw;
	}
	this->source = oldSource;
	return v.second;
}

TMPL T_TYPE(StringIt) Script<CFG>::Frame::parse(const StringIt& begin, const StringIt& end, bool literal) {
	assert(begin <= end);
	StringIt p = begin, e = end;
	XValue dummy;
	if (!literal) expr(p, end, dummy, true, true, STATEMENT);
	else switch (p < e ? *p : 0) {
		case 'f': if (!token(p, e, STR("alse")) && token(p, e, STR("unction"))) pre(p = begin, e, dummy, true); break;
		case 't': token(p, e, STR("rue")); break;
		case 'v': token(p, e, STR("oid")); break;
		case '+': case '-': if (token(p, e, STR("infinity")) || p + 1 >= e || p[1] < '0' || p[1] > '9') break;
		case '<': case '>': case '0': case '\'': case '"': case '1': case '2': case '3': case '4': case '5': case '6':
		case '7': case '8': case '9': pre(p, e, dummy, true); break;
	}
	return p;
}

TMPL T_TYPE(Value) Script<CFG>::Frame::call(const String& callee, const Value& body, long argc, const Value* argv) {
	assert(argc >= 0);
	typename CFG::Locals locals;
	Frame calleeFrame(locals, root, this);
	locals.assign(STR("$n"), argc);
	for (long i = 0; i < argc; ++i) locals.assign(String(STR("$")) += intToString<String>(i), argv[i]);
	if (!callee.empty()) locals.assign(STR("$callee"), callee);
	return calleeFrame.execute(body.empty() ? get(callee, true) : body);
}

TMPL void Script<CFG>::Frame::registerNative(const String& identifier, Native* native) {
	std::pair<Frame*, String> p = resolveFrame(identifier);
	if (!p.first->vars.assignNative(p.second, native))
		throw Xception(String(STR("Cannot register native: ")) += escape(identifier));
	if (native != 0)
		p.first->set(p.second, (String(STR("<")) += (p.first == &root ? p.second : p.first->label + p.second)) += '>');
}

/* --- Root --- */

TMPL Script<CFG>::Root::Root(Variables& vars) : Frame(vars, *this, 0), traceLevel(NO_TRACE), isInsideTracer(false)
		, autoLabelStart(autoLabel + 29) {
	std::fill_n(autoLabel, 32, ':');
}

TMPL T_TYPE(String) Script<CFG>::Root::generateLabel() {
	Char* b = autoLabelStart, * p = autoLabel + 30;
	while (*--p == 'z');
	switch (*p) {
		case ':':	*p = '1'; *--b = ':'; autoLabelStart = b; break;
		case '9':	*p = 'A'; break;
		case 'Z':	*p = 'a'; break;
		default:	(*p)++; break;
	}
	for (++p; *p != ':'; ++p) *p = '0';
	return String(const_cast<const Char*>(b), autoLabel + 31 - b);
}

TMPL void Script<CFG>::Root::setTracer(Precedence traceLevel, const Value& tracerFunction) throw() {
	this->traceLevel = traceLevel;
	this->tracerFunction = tracerFunction;
}

TMPL void Script<CFG>::Root::trace(Frame& frame, const String& source, SizeType offset, bool lvalue, const Value& value
		, Precedence level, bool exit) {
	if (!tracerFunction.isVoid() && !isInsideTracer) {
		try {
			isInsideTracer = true;
			Value argv[6] = { source, static_cast<ulong>(offset), lvalue, value, int(level), exit };
			frame.call(String(), tracerFunction, 6, argv);
			isInsideTracer = false;
		} catch (...) {
			isInsideTracer = false;
			setTracer(NO_TRACE, Value());																				// Turn off tracing on uncaught exceptions.
			throw;
		}
	}
}

/* --- STLVariables --- */

TMPL bool Script<CFG>::STLVariables::lookup(const String& symbol, Value& result) {
	typename VariableMap::const_iterator it = vars.find(symbol);
	if (it == vars.end()) return false;
	result = it->second;
	return true;
}

TMPL void Script<CFG>::STLVariables::list(const String& key, typename Variables::VarList& list) {
	for (typename VariableMap::const_iterator it = vars.lower_bound(key)
			; it != vars.end() && it->first.substr(0, key.size()) == key; ++it) list.push_back(*it);
}

TMPL T_TYPE(Native*) Script<CFG>::STLVariables::lookupNative(const String& identifier) {
	typename NativeMap::iterator it = natives.find(identifier);
	return (it == natives.end() ? 0 : it->second);
}

TMPL bool Script<CFG>::STLVariables::assignNative(const String& identifier, Native* native) {
	typename NativeMap::iterator it = natives.insert(typename NativeMap::value_type(identifier, (Native*)(0))).first;
	if (it->second != native) delete it->second;
	it->second = native;
	return true;
}

TMPL Script<CFG>::STLVariables::~STLVariables() {
	for (typename NativeMap::iterator it = natives.begin(); it != natives.end(); ++it) delete it->second;
}

TMPL std::pair<T_TYPE(Value), T_TYPE(String)> Script<CFG>::getThisAndMethod(Frame& f) {
	const String fn = f.get(STR("$callee"));
	StringIt it = std::find(fn.rbegin(), fn.rend(), '.').base();
	if (it <= fn.begin()) throw Xception(STR("Non-method call"));
	return std::pair<Value, String>(f.getPrevious().reference(String(fn.begin(), it - 1)), String(it, fn.end()));
}

/* --- Standard Library --- */

TMPL T_TYPE(Value) Script<CFG>::lib::elevate(Frame& f) { return f.execute(f.get(getThisAndMethod(f).first, true)); }
TMPL ulong Script<CFG>::lib::length(const String& s) { return static_cast<ulong>(s.size()); }
TMPL void Script<CFG>::lib::print(const String& s) { xcout<Char>() << std::basic_string<Char>(s) << std::endl; }
TMPL double Script<CFG>::lib::random(double m) { return m * rand() / double(RAND_MAX); }
TMPL T_TYPE(String) Script<CFG>::lib::reverse(String s) { std::reverse(s.begin(), s.end()); return s; }
TMPL void Script<CFG>::lib::thrower(const String& s) { throw Xception(s); }
TMPL T_TYPE(Value) Script<CFG>::lib::time(const Frame&) { return double(::time(0)); }

TMPL T_TYPE(String) Script<CFG>::lib::upper(String s) {
	transform(s.begin(), s.end(), s.begin(), std::bind2nd(std::ptr_fun(std::toupper<Char>), std::locale::classic()));
	return s;
}

TMPL T_TYPE(String) Script<CFG>::lib::lower(String s) {
	transform(s.begin(), s.end(), s.begin(), std::bind2nd(std::ptr_fun(std::tolower<Char>), std::locale::classic()));
	return s;
}

TMPL T_TYPE(String) Script<CFG>::lib::character(double d) {
	if (ushortChar(Char(d)) != d) throw Xception(String(STR("Illegal character code: ")) += doubleToString<String>(d));
	return String(1, Char(d));
}

TMPL uint Script<CFG>::lib::ordinal(const String& s) {
	if (s.size() != 1) throw Xception(String(STR("Value is not single character: ")) += escape(s));
	return ushortChar(s[0]);
}

TMPL bool Script<CFG>::lib::deleter(const Frame& f) {
	Value x = f.get(STR("$0"));
	std::pair<Frame*, String> fs = f.getPrevious().resolveFrame(x);
	return fs.first->getVariables().erase(fs.second);
}

TMPL T_TYPE(Value) Script<CFG>::lib::evaluate(const Frame& f) {
	return f.resolveFrame(f.getOptional(STR("$1"))).first->evaluate(f.get(STR("$0")));
}

TMPL bool Script<CFG>::lib::exists(const Frame& f) {
	Value x = f.get(STR("$0"));
	Value result;
	std::pair<Frame*, String> fs = f.getPrevious().resolveFrame(x);
	return fs.first->getVariables().lookup(fs.second, result);
}

TMPL ulong Script<CFG>::lib::find(const String& a, const String& b) {
	return static_cast<ulong>(find_first_of(a.begin(), a.end(), b.begin(), b.end()) - a.begin());
}

TMPL void Script<CFG>::lib::foreach(Frame& f) {
	Value arg1 = f.get(STR("$1"));
	std::pair<Frame*, String> fs = f.getPrevious().resolveFrame(f.get(STR("$0"))[Value()]);
	typename Variables::VarList list;
	fs.first->getVariables().list(fs.second, list);
	for (typename Variables::VarList::const_iterator it = list.begin(); it != list.end(); ++it) {
		Value argv[3] = { fs.first->reference(it->first), it->first.substr(fs.second.size()), it->second };
		f.call(String(), arg1, 3, argv);
	}
}

TMPL T_TYPE(String) Script<CFG>::lib::input(const String& prompt) {
	xcout<Char>() << std::basic_string<Char>(prompt);
	std::basic_string<Char> s;
	std::basic_istream<Char>& instream = xcin<Char>();
	if (instream.eof()) throw Xception(STR("Unexpected end of input file"));
	if (!instream.good()) throw Xception(STR("Input file error"));
	getline(instream, s);
	return s;
}

TMPL T_TYPE(Value) Script<CFG>::lib::invoke(Frame& f) {
	Value source = f.get(STR("$2")), arg4 = f.getOptional(STR("$4"));
	long offset = long(f.getOptional(STR("$3"), 0));
	long n = arg4.isVoid() ? long(f.get(source[String(STR("n"))])) - offset : long(arg4);
	if (n < 0) throw Xception(STR("Too few array elements"));
	std::vector<Value> a(n);
	for (long i = 0; i < long(a.size()); ++i) a[i] = f.get(source[i + offset]);
	return f.call(f.getOptional(STR("$0")), f.getOptional(STR("$1")), long(a.size()), a.empty() ? 0 : &a[0]);
}

TMPL T_TYPE(String) Script<CFG>::lib::load(const String& file) {
	std::basic_ifstream<Char> instream(toStdString(file).c_str());															// Sorry, can't pass a wchar_t filename. MSVC supports it, but it is non-standard. So we convert to a std::string to be on the safe side.
	if (!instream.good()) throw Xception(String(STR("Cannot open file for reading: ")) += escape(file));
	String chars;
	while (!instream.eof()) {
		if (!instream.good()) throw Xception(String(STR("Error reading from file: ")) += escape(file));
		Char buffer[4096];
		instream.read(buffer, 4096);
		chars += String(buffer, static_cast<typename String::size_type>(instream.gcount()));
	}
	return chars;
}

TMPL ulong Script<CFG>::lib::mismatch(const String& a, const String& b) {
	if (a.size() > b.size()) return static_cast<ulong>(std::mismatch(b.begin(), b.end(), a.begin()).first - b.begin());
	else return static_cast<ulong>(std::mismatch(a.begin(), a.end(), b.begin()).first - a.begin());
}

TMPL ulong Script<CFG>::lib::parse(Frame& f) {
	const String source = f.get(STR("$0"));
	return static_cast<ulong>(f.parse(source.begin(), source.end(), f.get(STR("$1"))) - source.begin());
}

TMPL T_TYPE(String) Script<CFG>::lib::radix(const Frame& f) {
	int radix = f.get(STR("$1"));
	if (radix < 2 || radix > 16) throw Xception(String(STR("Radix out of range: ")) += intToString<String>(radix));
	int minLength = f.getOptional(STR("$2"), 1);
	if (minLength < 0 || minLength > int(sizeof (int) * 8))
		throw Xception(String(STR("Minimum length out of range: ")) += intToString<String>(minLength));
	return intToString<String, ulong>(f.get(STR("$0")), f.get(STR("$1")), minLength);
}

TMPL void Script<CFG>::lib::save(const String& file, const String& chars) {
	std::basic_ofstream<Char> outstream(toStdString(file).c_str());															// Sorry, can't pass a wchar_t filename. MSVC supports it, but it is non-standard. So we convert to a std::string to be on the safe side.
	if (!outstream.good()) throw Xception(String(STR("Cannot open file for writing: ")) += escape(file));
	outstream.write(chars.data(), chars.size());
	if (!outstream.good()) throw Xception(String(STR("Error writing to file: ")) += escape(file));
}

TMPL ulong Script<CFG>::lib::search(const String& a, const String& b) {
	return static_cast<ulong>(std::search(a.begin(), a.end(), b.begin(), b.end()) - a.begin());
}

TMPL ulong Script<CFG>::lib::span(const String& a, const String& b) {
	typename String::const_iterator it;
	for (it = a.begin(); it != a.end() && std::find(b.begin(), b.end(), *it) != b.end(); ++it);
	return static_cast<ulong>(it - a.begin());
}

TMPL T_TYPE(String) Script<CFG>::lib::precision(const Frame& f) {
	return doubleToString<String>(f.get(STR("$0")), mini(maxi(int(f.get(STR("$1"))), 1), 16));
}

TMPL int Script<CFG>::lib::system(const String& command) {
	int xc = (command.empty() ? -1 : ::system(toStdString(command).c_str()));
	if (xc < 0) throw Xception(String(STR("Error executing system command: ")) += escape(command));
	return xc;
}

TMPL void Script<CFG>::lib::trace(const Frame& f) {
	f.getRoot().setTracer(Precedence(int(f.getOptional(STR("$1"), int(TRACE_CALL)))), f.getOptional(STR("$0")));
}

TMPL T_TYPE(Value) Script<CFG>::lib::tryer(Frame& f) {
	try { f.call(String(), f.get(STR("$0")), 0); } catch (const Xception& x) { return x.getError(); }
	return Value();
}

TMPL void Script<CFG>::addLibraryNatives(Frame& f, bool includeIO) {
	f.set(STR("VERSION"), PIKA_SCRIPT_VERSION);
	f.set(STR("run"), STR(">::evaluate((>{ $s = load($0); if ($s{:2} == '#!') $s{find($s, \"\\n\"):} })($0), @$)"));	// Note: we need this as a "bootstrap" to include 'stdlib.pika'.
	f.registerNative(STR("abs"), (double (*)(double))(fabs));
	f.registerNative(STR("acos"), (double (*)(double))(acos));
	f.registerNative(STR("asin"), (double (*)(double))(asin));
	f.registerNative(STR("atan"), (double (*)(double))(atan));
	f.registerNative(STR("atan2"), (double (*)(double, double))(atan2));
	f.registerNative(STR("ceil"), (double (*)(double))(ceil));
	f.registerNative(STR("char"), lib::character);
	f.registerNative(STR("cos"), (double (*)(double))(cos));
	f.registerNative(STR("cosh"), (double (*)(double))(cosh));
	f.registerNative(STR("delete"), lib::deleter);
	f.registerNative(STR("escape"), (String (*)(const String&))(escape));
	f.registerNative(STR("exists"), lib::exists);
	f.registerNative(STR("elevate"), lib::elevate);
	f.registerNative(STR("evaluate"), lib::evaluate);
	f.registerNative(STR("exp"), (double (*)(double))(exp));
	f.registerNative(STR("find"), lib::find);
	f.registerNative(STR("floor"), (double (*)(double))(floor));
	f.registerNative(STR("foreach"), lib::foreach);
	f.set(STR("include"), STR(">::if (!exists(@::included[$0])) { invoke('run',, @$); ::included[$0] = true }"));		// Note: we need this as a "bootstrap" to include 'stdlib.pika'.
	if (includeIO) f.registerNative(STR("input"), lib::input);
	f.registerNative(STR("invoke"), lib::invoke);
	f.registerNative(STR("length"), lib::length);
	f.registerNative(STR("log"), (double (*)(double))(log));
	f.registerNative(STR("log10"), (double (*)(double))(log10));
	if (includeIO) f.registerNative(STR("load"), lib::load);
	f.registerNative(STR("lower"), lib::lower);
	f.registerNative(STR("mismatch"), lib::mismatch);
	f.registerNative(STR("ordinal"), lib::ordinal);
	f.registerNative(STR("pow"), (double (*)(double, double))(pow));
	f.registerNative(STR("parse"), lib::parse);
	f.registerNative(STR("precision"), lib::precision);
	if (includeIO) f.registerNative(STR("print"), lib::print);
	f.registerNative(STR("radix"), lib::radix);
	f.registerNative(STR("random"), lib::random);
	f.registerNative(STR("reverse"), lib::reverse);
	f.registerNative(STR("sin"), (double (*)(double))(sin));
	f.registerNative(STR("sinh"), (double (*)(double))(sinh));
	if (includeIO) f.registerNative(STR("save"), lib::save);
	f.registerNative(STR("search"), lib::search);
	f.registerNative(STR("span"), lib::span);
	f.registerNative(STR("sqrt"), (double (*)(double))(sqrt));
	if (includeIO) f.registerNative(STR("system"), lib::system);
	f.registerNative(STR("tan"), (double (*)(double))(tan));
	f.registerNative(STR("tanh"), (double (*)(double))(tanh));
	f.registerNative(STR("time"), lib::time);
	f.registerNative(STR("throw"), lib::thrower);
	f.registerNative(STR("trace"), lib::trace);
	f.registerNative(STR("try"), lib::tryer);
	f.registerNative(STR("upper"), lib::upper);
}

TMPL Script<CFG>::Native::~Native() { }
TMPL Script<CFG>::Variables::~Variables() { }

#undef TMPL
#undef T_TYPE
#undef STR

} // namespace Pika

#endif
