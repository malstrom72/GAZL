/**
	\file PikaScript.h
	
	PikaScript is a high-level scripting language written in C++.
	
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

#ifndef PikaScript_h
#define PikaScript_h

#include "assert.h" // Note: I always include assert.h like this so that you can override it with a "local" file.
#include <vector>
#include <map>
#include <string>
#include <functional>

// These are defined as macros in the Windows headers and collide with some of our "proper" C++ definitions. Sorry, it
// just ain't right to use global macros in C++. I #undef them. Include PikaScript.h before the Windows headers if you
// need these macros (or push and pop their definitions before and after including this header with #pragma push_macro
// and pop_macro).

#undef VOID
#undef ERROR

/**
	The PikaScript namespace.
*/
namespace Pika {

#if (PIKA_UNICODE)
	#define STR(s) L##s
	#define PIKA_SCRIPT_VERSION L"0.97"
#else
	#define STR(x) x
	#define PIKA_SCRIPT_VERSION "0.97"
#endif

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;

/**
	\name Conversion routines for string <-> other types.

	For some of these we could use stdlib implementations yes, but:

	-# Some of them (e.g. atof, strtod) behaves differently depending on global "locale" setting. We can't have that.
	-# The stdlib implementations can be slow (e.g. my double->string conversion is about 3 times faster than MSVC CRT).
	-# Pika requires high-precision string representation and proper handling of trailing 9's etc.
*/
//@{

template<class S> std::string toStdString(const S& s);																	///< Converts the string \p s to a standard C++ string. \details The default implementation is std::string(s.begin(), s.end()). You should specialize this template if necessary.
template<class S> ulong hexToLong(typename S::const_iterator& p, const typename S::const_iterator& e);					///< Converts a string in hexadecimal form to an ulong integer. \details \p p is updated on return to point to the first unparsed (e.g. invalid) character. If \p p == \p e, the full string was successfully converted.
template<class S> long stringToLong(typename S::const_iterator& p, const typename S::const_iterator& e);				///< Converts a string in decimal form to a signed long integer. \details \p p is updated on return to point to the first unparsed (e.g. invalid) character. If \p p == \p e, the full string was successfully converted.
template<class S, typename T> S intToString(T i, int radix = 10, int minLength = 1);									///< Converts the integer \p i to a string with a radix and minimum length of your choice. \details \p radix can be anything between 1 (binary) and 16 (hexadecimal).
template<class S> double stringToDouble(typename S::const_iterator& p, const typename S::const_iterator& e);			///< Converts a string in scientific e notation (e.g. -12.34e-3) to a double floating point value. \details Spaces before 'e' are not accepted. Uppercase 'E' is allowed. Positive and negative 'infinity' is supported (provided the compiler allows it).\p p is updated on return to point to the first unparsed (e.g. invalid) character. If \p p == \p e, the full string was successfully converted.
template<class S> bool stringToDouble(const S& s, double& d);															///< A convenient utility routine that tries to convert the entire string \p s (in scientific e notation) to a double, returning true on success or false if the string is not in valid syntax.
template<class S> S doubleToString(double d, int precision = 14);														///< Converts the double \p d to a string (in scientific e notation, e.g. -12.34e-3). \details \p precision can be between 1 and 24 and is the number of digits to include in the output string (not counting any exponent of course). Any trailing decimal zeroes will be trimmed and only significant digits will be included.
template<class S> S unescape(typename S::const_iterator& p, const typename S::const_iterator& e);						///< Converts a string that is either enclosed in single (' ') or double (" ") quotes. \details The routine expects the string beginning at \p p to be of one of these forms, but \p e can point beyond the terminating quote. If the single (' ') quote is used, the string between the quotes is simply extracted "as is" with the exception of pairs of apostrophes ('') that are used to represent single apostrophes. If the string is enclosed in double quotes (" ") it can use C-style escape sequences. The supported escape sequences are: @code \\ \" \' \a \b \f \n \r \t \v \xHH \uHHHH \<decimal> @endcode. On return, \p p will point to the first unparsed (e.g. invalid) character. If \p p == \p e, the full string was successfully converted.
template<class S> S escape(const S& s);																					///< Depending on the contents of the source string \p s it is encoded either in single (' ') or double (" ") quotes. \details If the string contains only printable ASCII chars (ASCII values between 32 and 126 inclusively) and no apostrophes (' '), it is enclosed in single quotes with no further processing. Otherwise it is enclosed in double quotes (" ") and any unprintable ASCII character, backslash (\) or quotation mark (\c ") is encoded using C-style escape sequences (e.g. \code "line1\nline2" \endcode).

//@}

/**
	bound_mem_fun_t is a member functor bound to a specific C++ object through a pointer. You may use this class instead
	of "manually" binding a std::mem_fun functor to an object. Besides being more convenient, this class solves a
	problem in some STL implementations that prevents you from having reference arguments in the functor.

	You would normally use the helper function bound_mem_fun() to automatically instantiate the correct template.
*/
template<class C, class A0, class R> class bound_mem_fun_t : public std::unary_function<A0, R> {
	public:		bound_mem_fun_t(R (C::*m)(A0), C* o) : m(m), o(o) { }
	public:		R operator()(A0 a) const { return (o->*m)(a); }
	protected:	R (C::*m)(A0);
	protected:	C* o;
};

/**
	bound_mem_fun creates a member functor bound to a specific C++ object through a pointer. You may use this function
	instead of "manually" binding a std::mem_fun functor to an object. For example the following code: \code
	std::bind1st(std::mem_fun(&Dancer::tapDance), fredAstaire))); \endcode can be replaced with \code
	bound_mem_fun(&Dancer::tapDance, fredAstaire); \endcode

	Furthermore, bound_mem_fun does not suffer from a problem that some STL implementations has which prevents you from
	using member functors with reference arguments.

	bound_mem_fun is used in PikaScript to directly bind a native function to a member function of a certain C++ object.
*/
template<class C, class A0, class R> inline bound_mem_fun_t<C, A0, R> bound_mem_fun(R (C::*m)(A0), C* o) {
	return bound_mem_fun_t<C, A0, R>(m, o);
}

/**
	We use this dummy class to specialize member functions for arbitrary types (including void, references etc).
*/
template<class T> class Dumb { };

/**
	The PikaScript exception class. It is based on \c std::exception and stores a simple error string of type \p S. The
	standard what()	is provided, and therefore conversion to a \c const \c char* string must be performed in case the
	\p S class is not directly compatible. Since what() is defined to return a pointer only, we need storage for the
	converted string within this class too (Exception::converted).
*/
template<class S> class Exception : public std::exception {
	public:		Exception(const S& error) : error(error) { }															///< Simply constructs the exception with error string \p error.
	public:		virtual S getError() const throw() { return error; }													///< Return the error string for this exception.
	public:		virtual const char *what() const throw() { return (converted = toStdString(error)).c_str(); }			///< Returns the error as a null-terminated char string. \details Use getError() if you can since it is faster and permits other character types than char.
	public:		virtual ~Exception() throw() { }																		// (GCC requires explicit destructor with one that has throw().)
	protected:	S error;																								///< The error string.
	protected:	mutable std::string converted;																			///< Since what() is defined to return a pointer only, we need storage for the converted string within this class too.
};

/**
	STLValue is the reference implementation of a PikaScript variable. Internally, all values are represented by an STL
	compliant string class \p S *. This class actually inherits from \p S (with public inheritance) for optimization
	reasons. This way we avoid a lot of unnecessary temporary objects when we cast to and from strings. (Unfortunately
	it is not possible to make this inheritance private and add conversion operators to the \p S class. Explicit
	conversion operators in C++ have lower priority than implicit base class conversions.)

	Although it may seem inefficient to store all variables in textual representation it makes PikaScript easy to
	interface with and debug for. With the custom value <-> text conversion routines in PikaScript the performance isn't
	too bad. It mainly depends on the performance of the string implementation which is the reason why this class is a
	template. The standard variant of STLValue uses std::string, but you may want to "plug in" a more efficient class.

	STLValue supports construction from and casting to the following C++ types:

	- \c bool
	- \c int
	- \c uint
	- \c long
	- \c ulong
	- \c float
	- \c double
	- the template string class \p S

	\note
	* PikaScript only requires a specific subset of the STL string features. Utility member functions (such as \c
	find_first_of) are never used (instead, the equivalent generic STL algorithms are utilized). Destructive functions
	such as \c insert and \c erase are not used either. Furthermore, PikaScript consider all string access through the
	subscript operator [] to be for reading only (therefore only a \c const function for this operator is required).
*/
template<class S> class STLValue : public S {

	/// \name Typedefs.
	//@{
	public:		typedef S String;																						///< The class to use for all strings (i.e. the super-class).
	public:		typedef typename S::value_type Char;																	///< The character type for all strings (e.g. char or wchar_t).
	//@}
	/// \name Constructors.
	//@{
	public:		STLValue() { }																							///< The default constructor initializes the value to the empty string (or "void").
	public:		STLValue(double d) : S(doubleToString<S>(d)) { }														///< Constructs a value representing the double precision floating point \p d.
	public:		STLValue(float f) : S(doubleToString<S>(f)) { }															///< Constructs a value representing the single precision floating point \p f.
	public:		STLValue(long i) : S(intToString<S, long>(i)) { }														///< Constructs a value representing the signed long integer \p l.
	public:		STLValue(ulong i) : S(intToString<S, ulong>(i)) { }														///< Constructs a value representing the ulong integer \p l.
	public:		STLValue(int i) : S(intToString<S, long>(i)) { }														///< Constructs a value representing the signed integer \p i.
	public:		STLValue(uint i) : S(intToString<S, ulong>(i)) { }														///< Constructs a value representing the unsigned integer \p i.
	public:		STLValue(bool b) : S(b ? S(STR("true")) : S(STR("false"))) { }											///< Constructs a value representing the boolean \p b.
	public:		template<class T> STLValue(const T& s) : S(s) { }														///< Pass other types of construction onwards to the super-class \p S.
	//@}
	/// \name Conversion to native C++ types.
	//@{
	public:		operator bool() const;																					///< Converts the value to a boolean. \details If the value isn't "true" or "false" an exception is thrown.
	public:		operator long() const;																					///< Converts the value to a signed long integer. \details If the value isn't in valid integer format an exception is thrown.
	public:		operator double() const;																				///< Converts the value to a double precision floating point. \details If the value isn't in valid floating point format an exception is thrown.
	public:		operator float() const { return float(double(*this)); }													///< Converts the value to a single precision floating point. \details If the value isn't in valid floating point format an exception is thrown.
	public:		operator ulong() const { return ulong(long(*this)); }													///< Converts the value to an ulong integer. \details If the value isn't in valid integer format an exception is thrown.
	public:		operator int() const { return int(long(*this)); }														///< Converts the value to a signed integer. \details If the value isn't in valid integer format an exception is thrown.
	public:		operator uint() const { return uint(int(*this)); }														///< Converts the value to an unsigned integer. \details If the value isn't in valid integer format an exception is thrown.
	//@}
	/// \name Overloaded operators (comparisons and subscript).
	//@{
	public:		bool operator<(const STLValue& r) const;																///< Less than comparison operator. \details Notice that numbers are compared numerically and a number is always considered less than any non-number string.
	public:		bool operator==(const STLValue& r) const;																///< Equality operator. \details Notice that numbers are compared numerically (e.g. '1.0' and '1' are considered identical) and strings are compared literally (character by character).
	public:		bool operator!=(const STLValue& r) const { return !(*this == r); }										///< Non-equality operator. \details Notice that numbers are compared numerically (e.g. '1.0' and '1' are considered identical) and strings are compared literally (character by character).
	public:		bool operator>(const STLValue& r) const { return r < (*this); }											///< Greater than comparison operator. \details Notice that numbers are compared numerically and a number is always considered less than any non-number string.
	public:		bool operator<=(const STLValue& r) const { return !(r < (*this)); }										///< Less than or equal to comparison operator. \details Notice that numbers are compared numerically and a number is always considered less than any non-number string.
	public:		bool operator>=(const STLValue& r) const { return !((*this) < r); }										///< Greater than or equal to comparison operator. \details Notice that numbers are compared numerically and a number is always considered less than any non-number string.
	public:		const STLValue operator[](const STLValue& i) const;														///< The subscript operator returns the concatenation of the value with the dot (.) separator (if necessary) and the value \p i. \details Use it on a reference value to create a reference to a subscript element of that reference.
	//@}
	/// \name Classification methods.
	//@{
	public:		bool isVoid() const { return S::empty(); }																///< Returns true if the value represents the empty string.
	//@}
	/// \name Helper templates to allow certain operations on any type that is convertible to a STLValue.
	//@{
	public:		template<class T> bool operator<(const T& r) const { return operator<(STLValue(r)); }
	public:		template<class T> bool operator==(const T& r) const { return operator==(STLValue(r)); }
	public:		template<class T> bool operator!=(const T& r) const { return operator!=(STLValue(r)); }
	public:		template<class T> bool operator>(const T& r) const { return operator>(STLValue(r)); }
	public:		template<class T> bool operator<=(const T& r) const { return operator<=(STLValue(r)); }
	public:		template<class T> bool operator>=(const T& r) const { return operator>=(STLValue(r)); }
	public:		template<class T> const STLValue operator[](const T& i) const { return operator[](STLValue(i)); }
	//@}
};

/**	Precedence levels are used both internally for the parser and externally for the tracing mechanism. */
enum Precedence {
	NO_TRACE = 0		///< used only for tracing with tick()
	, TRACE_ERROR = 1	///< used only for tracing with tick()
	, TRACE_CALL = 2	///< used only for tracing with tick()
	, TRACE_LOOP = 3	///< used only for tracing with tick()
	, STATEMENT = 4		///< x; y;
	, BODY = 5			///< if () x, for () x
	, ARGUMENT = 6		///< (x, y)
	, BRACKETS = 7		///< (x) [x]
	, ASSIGN = 8		///< x=y x*=y x/=y x\=y x%=y x+=y x-=y x<<=y x>>=y x#=y x&=y x^=y x|=y
	, LOGICAL_OR = 9	///< x||y
	, LOGICAL_AND = 10	///< x&&y
	, BIT_OR = 11		///< x|y
	, BIT_XOR = 12		///< x^y
	, BIT_AND = 13		///< x&y
	, EQUALITY = 14		///< x===y x==y x!==y x!=y
	, COMPARE = 15		///< x<y x<=y x>y x>=y
	, CONCAT = 16		///< x#y
	, SHIFT = 17		///< x<<y x\>>y
	, ADD_SUB = 18		///< x+y x-y
	, MUL_DIV = 19		///< x*y x/y x\y x%y
	, PREFIX = 20 		///< \@x !x ~x +x -x ++x --x
	, POSTFIX = 21		///< x() x.y x[y] x{y} x++ x--
	, DEFINITION = 22	///< function { }
};

/**
	Script is a meta-class that groups all the core classes of the PikaScript interpreter together (except for the value
	class). The benefit of having a class like this is that we can declare types that are common to all sub-classes.

	The class is a template that takes another meta-class for configuring PikaScript. The configuration class should
	contain the following typedefs:

	-# \c Value			(use this class for all PikaScript values, e.g. STLValue<std::string>)
	-# \c Locals		(when a function call occurs, this sub-class of Variables will be instantiated for the callee)
	-# \c Globals		(this sub-class of Variables is used for the FullRoot class)
*/
template<class Config> struct Script {

	typedef typename Config::Value Value;																				///< The class used for all values and variables (defined by the configuration meta-class). E.g. STLValue.
	typedef typename Value::String String;																				///< The class used for strings (defined by the string class). E.g. \c std::string.
	typedef typename String::value_type Char;																			///< The character type for all strings (defined by the string class). E.g. \c char.
	typedef typename String::size_type SizeType;																		///< The length type for all strings (defined by the string class). E.g. \c size_t.
	typedef typename String::const_iterator StringIt;																	///< The const_iterator of the string is used so frequently it deserves its own typedef.
	typedef Exception<String> Xception;																					///< The exception type.

	class Native;
	class Root;

	/**
		Variables is an abstract base class which implements the interface to the variable space that a Frame works on.
		In the configuration meta-class class (Script::Config) two typedefs exist that determines which sub-classes of
		Variables should be used for the "root frame" (= Globals) and subsequently for the "sub-frames" (= Locals).

		A standard Variables class is supplied in this header file (STLVariables). Custom sub-classes are useful for
		optimization and special integration needs.

		Notice that the separation of Frames and Variables makes it possible to have more than one Frame referencing
		the same variable space. This could be useful for example in a threaded situation where several concurrent
		threads running PikaScript should share global variables. In this case each thread should still have a distinct
		"root frame" and you need to implement a sub-class of Variables that accesses its data in a thread-safe manner.
	*/
	class Variables {
		public:		typedef Script ForScript;
		public:		typedef std::vector< std::pair<String, Value> > VarList;
		public:		virtual bool lookup(const String& symbol, Value& result) = 0;										///< Lookup \p symbol. \details If found, store the found value in \p result and return true, otherwise return false.
		public:		virtual bool assign(const String& symbol, const Value& value) = 0;									///< Assign \p value to \p symbol and return true if the assignment succeeded. \details If false is returned, the calling Frame::set() will throw an exception.
		public:		virtual bool erase(const String& symbol) = 0;														///< Erase \p symbol. Return true if the symbol existed and was successfully erased.
		public:		virtual void list(const String& key, VarList& list) = 0;											///< Iterate all symbols that begins with \p key and push back names and values to \p list. \details There are no requirements on the order of the listed elements. You should not erase the list at the beginning.
		public:		virtual Native* lookupNative(const String& identifier) = 0;											///< Lookup the native function (or object) with \p identifier. \details Return 0 if the native could not be found. In this case, the caller will throw an exception.
		public:		virtual bool assignNative(const String& identifier, Native* native) = 0;							///< Assign the native function (or object) \p native to \p identifier, replacing any already existing definition. \details Once assigned, the native is considered "owned" by this variable space. This class is responsible for deleting its natives on destruction and also delete the existing definition when an identifier is being reassigned.
		public:		virtual ~Variables();																				///< Destructor. \details Don't forget to delete all registered natives.
	};

	/**
		The execution context and interpreter for PikaScript.

		This is where the magic happens. A Frame represents an execution context for a PikaScript function and it
		contains the source code interpreter. Normally you do not create instances of Frame yourself. They are created
		on stack whenever a function call is made. Notice that this implementation of PikaScript does not run in a
		virtual machine, instead it is interpreted directly and it shares calling stack etc with your C++ application.
	*/
	class Frame {

		/// \name Construction.
		//@{
		public:		Frame(Variables& vars, Root& root, Frame* previous);												///< Constructs the Frame and associates it with the variable space \p vars. \details All frames on the calling stack have direct access to the "root frame" which is designated by \p root (will be = \c *this for the Root). \p previous should point to the caller Frame (or 0 for the Root). The "frame label" of a root frame is always \c :: . Root::generateLabel() is called to create unique labels for other frames.
		//@}
		/// \name Properties.
		//@{
		public:		Variables& getVariables() const throw() { return vars; }											///< Returns a reference to the Variable instance associated with this Frame. Simple as that.
		public:		Root& getRoot() const throw() { return root; }														///< Returns a reference to the "root frame" for this Frame. (No brainer.)
		public:		Frame& getPrevious() const throw() { assert(previous != 0); return *previous; }						///< Returns a reference to the previous frame (i.e. the frame of the caller of this frame). Must not be called on the root frame (will assert).
		//@}
		/// \name Getting, setting and referencing variables.
		//@{
		public:		Value get(const String& identifier, bool fallback = false) const;									///< Gets a variable value. \details If \p identifier is prefixed with a "frame identifier" (e.g. a "frame label" or \c ^), it will be "resolved" and used for retrieving the variable. Otherwise the variable space associated with this Frame instance will be checked and if the variable cannot be found and \p fallback is true, the global variable space will also be checked. If the variable cannot be found in any of the checked locations, an exception will be thrown.
		public:		Value getOptional(const String& identifier, const Value& defaultValue = Value()) const;				///< Tries to get the variable value as with get() (but never "falls back"). \details If the variable cannot be found, \p defaultValue will be returned instead.
		public:		const Value& set(const String& identifier, const Value& v);											///< Sets a variable value. \details Just as with get(), \p identifier may be prefixed with a "frame identifier" to address a different Frame.
		public:		Value reference(const String& identifier) const;													///< Creates a reference to the variable identified by \p identifier by prefixing it with a "frame label". \details If the identifier is already prefixed with a "frame identifier" (such as \c ^) it will be resolved to determine the frame. Otherwise, the label of this Frame instance is used.
		public:		std::pair< Frame*, String > resolveFrame(const String& identifier) const;							///< Resolves the frame for \p identifier and returns it together with \p identifier stripped of any prefixed "frame identifier". \details The rules are as follows: 1) If the identifier has a leading \c ::, the "root frame" is returned. 2) If the identifier begins with an existing frame label, this frame is used for resolving the rest of the identifier. (If a frame label cannot be found an exception is thrown.) 3) For each leading '^' the previous frame is used for resolving the rest of the identifier. 4) Finally, if the identifier does not begin with the '$' character, the "closure" of the current frame is returned, otherwise the current frame is returned.
		//@}
		/// \name Calling functions and evaluating source code.
		//@{
		public:		Value call(const String& callee, const Value& body, long argc, const Value* argv = 0);				///< Calls a Pika function (by setting up a new "sub-frame" and executing the function body). \details You may pass \c Value() (the \c void value) for \p callee or \p body. If only \p callee is specified, it will be used to retrieve the function body (through get()). If only \p body is specified, the called function will not have a \c $callee variable (\c $callee is used for debugging and object-oriented solutions). If both are present, the \c $callee variable will be set to \p callee, and \p body will be executed. \p argc is the number of arguments and if this is not zero, the \p argv parameter should point to an array of arguments (of at least \p argc elements in size). The return value is that of the PikaScript function.
		public:		Value execute(const String& body);																	///< A low-level function that executes \p body directly on the Frame instance. \details This means that unlike call(), you need to setup a "sub-frame" yourself, populate it with argument variables and then use this function. The return value is that of the PikaScript function.
		public:		Value evaluate(const String source);																///< Evaluates the PikaScript expression in \p source directly on this Frame. \details This differs from execute() in that \p source is not expected to be in the format of a "function body" of an "ordinary", "lambda" or "native" function. (Notice that we are not passing a reference to the \p source string here so that we are safe in case the PikaScript code manipulates the very string it is running on.) The return value is that of the evaluated expression.
		public:		StringIt parse(const StringIt& begin, const StringIt& end, bool literal);							///< Parses a PikaScript expression or literal (without evaluating it) and returns an iterator pointing at the end of the expression.
		//@}
		/// \name Registering native functions (or objects).
		//@{
		public:		void registerNative(const String& identifier, Native* native);										///< Registers the native function (or object) \p native with \p identifier in the appropriate variable space (determined by any "frame identifier" present in \p identifier). \details Once registered, the native is considered "owned" by the variable space. In other words, all registered natives will be deleted by the Variables destructor. Also, if you register a new native on an already used identifier, the old native for that identifier will be deleted automatically. Besides assigning the native with Variables::assignNative() this method also sets the variable \p identifier to \c <identifier> (unless \p native is a null-pointer).
		public:		template<class A0, class R> void registerNative(const String& i, R (*f)(A0)) {
						registerNative(i, newUnaryFunctor(std::ptr_fun(f)));
					}																									///< Helper template for easily registering a unary C++ function. \details The C++ function should take a single argument of either Frame& or any of the native types that are convertible from Script::Value. It should return a value of any type that is convertible to Script::Value or void.
		public:		template<class A0, class A1, class R> void registerNative(const String& i, R (*f)(A0, A1)) {
						registerNative(i, newBinaryFunctor(std::ptr_fun(f)));
					}																									///< Helper template for easily registering a binary C++ function. \details The C++ function should take two arguments of any of the native types that are convertible from Script::Value. It should return a value of any type that is convertible to Script::Value or void.
		public:		template<class C, class A0, class R> void registerNative(const String& i, C* o, R (C::*m)(A0)) {
						registerNative(i, newUnaryFunctor(bound_mem_fun(m, o)));
					}																									///< Helper template for easily registering a unary C++ member function in the C++ object pointed to by \p o. \details The C++ member function should take a single argument of either Frame& or any of the native types that are convertible from Script::Value. It should return a value of any type that is convertible to Script::Value or void. Normally, this registration technique is used for bridging Pika function calls to methods of a C++ object which is guaranteed to live as long as the target Frame.
		public:		void unregisterNative(const String& identifier) { registerNative(identifier, (Native*)(0)); }		///< Helper function for unregistering a native function / object. \details Unregistering a native is the same as registering a null-pointer to the identifier. Any PikaScript variable referring to \c <identifier> will still do so (including the one created automatically by registerNative()). However, performing a function call on such a variable will generate a run-time exception.
		//@}
		/// \name Destruction.
		//@{
		public:		virtual ~Frame() { }																				///< The default destructor does nothing, but it is always good practice to have a virtual destructor.
		//@}
		protected:	typedef std::pair<bool, Value> XValue;																///< The XValue differentiates lvalues and rvalues and is used internally in the interpreter. \details first = lvalue or not, second = symbol (for lvalue) or actual value (for rvalue).
		protected:	void tick(const StringIt& p, const XValue& v, Precedence thres, bool exit);
		protected:	Value rvalue(const XValue& v, bool fallback = true);
		protected:	const Value& lvalue(const XValue& v);
		protected:	void white(StringIt& p, const StringIt& e);
		protected:	bool token(StringIt& p, const StringIt& e, const Char* token);
		protected:	Frame* resolveFrame(StringIt& p, const StringIt& e) const;
		protected:	template<class F> bool binaryOp(StringIt& p, const StringIt& e, XValue& v, bool dry
							, Precedence thres, int hop, Precedence prec, F op);
		protected:	template<class F> bool assignableOp(StringIt& p, const StringIt& e, XValue& v, bool dry
							, Precedence thres, int hop, Precedence prec, F op);
		protected:	template<class F> bool addSubOp(StringIt& p, const StringIt& e, XValue& v, bool dry
							, Precedence thres, const F& f);
		protected:	template<class E, class I, class S> bool lgtOp(StringIt& p, const StringIt& e, XValue& v, bool dry
							, Precedence thres, const E& excl, const I& incl, S shift);
		protected:	bool pre(StringIt& p, const StringIt& e, XValue& v, bool dry);
		protected:	bool post(StringIt& p, const StringIt& e, XValue& v, bool dry, Precedence thres);
		protected:	bool expr(StringIt& p, const StringIt& e, XValue& v, bool emptyOk, bool dry, Precedence thres);
		protected:	bool termExpr(StringIt& p, const StringIt& e, XValue& v, bool emptyOk, bool dry, Precedence thres
							, Char term);
		protected:	static long intDiv(long x, long y);

		protected:	Variables& vars;
		protected:	Root& root;
		protected:	Frame* const previous;
		protected:	Frame* closure;
		protected:	const String* source;
		protected:	const String label;

		private:	Frame(const Frame& copy); // N/A
		private:	Frame& operator=(const Frame& copy); // N/A
	};

	/**
		The Root is the first Frame you instantiate. It is the starting point for the execution of PikaScript code. Its
		variables can be accessed from any frame with the special "frame identifier" \c ::. Furthermore, its variable
		space is often checked as a "backup" for symbols that cannot be retrieved from local "sub-frames".

		The class also offers a few functions out of which you may overload trace() and setTracer() if you want to
		customize the tracing mechanism in PikaScript. The default implementation calls a PikaScript function that you
		can designate with the standard library function "trace".

		In case you use PikaScript concurrently in different threads, you need a Root for every thread, but you could
		implement and share a sub-class of Variables that accesses shared data in a thread-safe manner.

		If you just want to use the standard Root implementation with a standard variable space you may want to use
		FullRoot instead.
	*/
	class Root : public Frame {
		public:		Root(Variables& vars);
		public:		virtual void trace(Frame& frame, const String& source, SizeType offset, bool lvalue
							, const Value& value, Precedence level, bool exit);											///< Overload this member function if you want to customize the tracing mechanism in PikaScript. \details The default implementation calls the PikaScript function Root::tracerFunction that you can assign with the standard library function "trace". See the standard library documentation on "trace" for more information on the arguments to this member function.
		public:		virtual void setTracer(Precedence traceLevel, const Value& tracerFunction) throw();					///< Called by the standard library function "trace" to assign a PikaScript tracer function and a trace level. (Also called by the standard trace() on exceptions.) \details You may want to overload this member function if you change the tracing mechanism and need control over the trace level for example.
		public:		bool doTrace(Precedence level) const throw() { return level <= traceLevel; }						///< \details This function is called *a lot*. For performance reasons it is good if it becomes inlined, so we are not declaring it virtual. If you want to customize which events that will be traced, try cleverly implementing your own trace() and setTracer() member functions instead.
		public:		String generateLabel();																				///< Each "sub-frame" requires a unique "frame label". \details This function creates it by "incrementing" Root::autoLabel, character by character, using '0' to '9' and upper and lower case 'a' to 'z', growing the string when necessary.
		protected:	Precedence traceLevel;																				///< Calls to trace() will only happen when the "precedence level" is less or equal to this. \details E.g. if traceLevel is CALL, only function calls and caught exceptions will be traced.
		protected:	Value tracerFunction;																				///< Pika-script tracer function (used by the default trace() implementation).
		protected:	bool isInsideTracer;																				///< Set to prevent recursive calling of tracer (used by the default trace() implementation).
		protected:	Char autoLabel[32];																					///< The last generated frame label (padded with leading ':').
		protected:	Char* autoLabelStart;																				///< The first character of the last generated frame label (begins at autoLabel + 30 and slowly moves backwards when necessary).
	};

	/**
		FullRoot inherits from both Root and Config::Globals (which should be a descendant to Variable). Its
		constructor adds the natives of the standard library. This means that by instantiating this class you will get
		a full execution environment for PikaScript ready to go.
	*/
	class FullRoot : public Root, public Config::Globals {
		public:		FullRoot(bool includeIONatives = true) : Root(*(typename Config::Globals*)(this)) {					///< If \p includeIO is false, 'load', 'save', 'input', 'print' and 'system' will not be registered.
						addLibraryNatives(*this, includeIONatives);
					}
	};

	/**
		STLVariables is the reference implementation of a variable space. It simply uses two std::map's for the
		PikaScript variables and the natives respectively. All registered natives are deleted on the destruction of this
		class.

		See Variables for descriptions on the overloaded member functions in this class.
	*/
	class STLVariables : public Variables {
		public:		typedef std::map<const String, Value> VariableMap;
		public:		typedef std::map<const String, Native* > NativeMap;
		public:		virtual bool lookup(const String& symbol, Value& result);
		public:		virtual bool assign(const String& symbol, const Value& value) { vars[symbol] = value; return true; }
		public:		virtual bool erase(const String& symbol) { return (vars.erase(symbol) > 0); }
		public:		virtual void list(const String& key, typename Variables::VarList& list);
		public:		virtual Native* lookupNative(const String& name);
		public:		virtual bool assignNative(const String& name, Native* native);
		public:		virtual ~STLVariables();
		public:		VariableMap vars;
		public:		NativeMap natives;
	};

	/**
		Native is the base class for the native functions and objects that can be accessed from PikaScript. It has a
		single virtual member function which should process a call to the native. Since natives are owned by the
		variable space once they are registered (and destroyed when the variable space destructs), they often act as
		simple bridges to other C++ functions and objects.

		The easiest way to register a native is by calling one of the Frame::registerNative() member functions
		(typically on the "root frame"). You will find a couple of template functions there that allows you to register
		functors directly. They will create the necessary Native classes for you in the background.
	*/
	class Native {
		public:		virtual Value pikaCall(Frame& /*frame*/) { throw Xception(STR("Not callable")); }					///< Process the PikaScript call. \details Arguments can be retrieved by getting \c $0, \c $1 etc from \p frame (via Frame::get() or Frame::getOptional()). If the function should not return a value, return \c Value() (which constructs a \c void value).
		public:		virtual ~Native();
	};

	/**
		We provide two Native template classes for bridging PikaScript calls to C++ "functors". One that takes a single
		argument (UnaryFunctor) and one that takes two arguments (BinaryFunctor). A "functor" is either a class that
		has an overloaded operator() or a C / C++ function. It is a concept introduced to C++ with STL so please refer
		to your STL documentation of choice for more info. (For example: http://www.sgi.com/tech/stl/functors.html )

		Thanks to some clever template tricks, these classes are very flexible when it comes to what type of arguments
		your functor can take and what type it may return. Here are your options:

		- Any argument can be of a type that is convertible from Script::Value (e.g., \c bool, \c long, \c double etc).
		- Likewise, the functor can return any type that is convertible to a Script::Value.
		- You can also use a functor with \c void result type.
		- The functor may take a single argument type of Script::Frame&. You can then retrieve all the arguments for
		the call by reading \c $0, \c $1, \c $2 etc from the Frame (via Frame::get() or Frame::getOptional()).

		In Frame you will find a template function (Frame::registerNative()) that allows you to register a native C++
		function directly through a functor. It will construct the proper functor instance for you "in the background".

		If you need use examples, please see the standard library implementation in PikaScriptImpl.h.
	*/
	template<class F, class A0 = typename F::argument_type, class R = typename F::result_type> class UnaryFunctor
			: public Native {
		public:		UnaryFunctor(const F& functor) : func(functor) { }
		protected:	template<class T> const Value arg(Frame& f, const T&) { return f.get(STR("$0")); }					///< Get the first argument from the frame \p f.
		protected:	Frame& arg(Frame& f, const Dumb<Frame&>&) { return f; }												///< Overloaded arg() that returns the actual frame instead of getting the argument value if the functor argument references a Script::Frame& type.
		protected:	const Frame& arg(Frame& f, const Dumb<const Frame&>&) { return f; }									///< Overloaded arg() that returns the actual frame instead of getting the argument value if the functor argument references a const Script::Frame& type.
		protected:	template<class A, class T> Value call(A& a, const Dumb<T>&) { return func(a); }						///< Call the functor with the argument \p a.
		protected:	template<class A> Value call(A& a, const Dumb<void>&) { func(a); return Value(); }					///< Overloaded to return the \c void value for functors that returns the \c void type.
		public:		virtual Value pikaCall(Frame& f) { return call(arg(f, Dumb<A0>()), Dumb<R>()); }
		protected:	F func;
	};

	/**
		See UnaryFunctor for documentation.
	*/
	template<class F, class A0 = typename F::first_argument_type, class A1 = typename F::second_argument_type
			, class R = typename F::result_type> class BinaryFunctor : public Native {
		public:		BinaryFunctor(const F& functor) : func(functor) { }
		protected:	template<class T> Value call(const Value& a0, const Value& a1, const T&) { return func(a0, a1); }	///< Call the functor with the arguments \p a0 and \p a1.
		protected:	Value call(const Value& a0, const Value& a1, const Dumb<void>&) { func(a0, a1); return Value(); }	///< Overloaded to return the \c void value for functors that returns the \c void type.
		public:		virtual Value pikaCall(Frame& f) { return call(f.get(STR("$0")), f.get(STR("$1")), Dumb<R>()); }
		protected:	F func;
	};

	template<class F> static UnaryFunctor<F>* newUnaryFunctor(const F& f) { return new UnaryFunctor<F>(f); }			///< Helper function to create a UnaryFunctor class with correct template parameters.
	template<class F> static BinaryFunctor<F>* newBinaryFunctor(const F& f) { return new BinaryFunctor<F>(f); }			///< Helper function to create a BinaryFunctor class with correct template parameters.

	/**
		getThisAndMethod splits the \c $callee variable of \p frame into object ("this") and method. The returned value
		is a pair, where the \c first value ("this") is a reference to the object and the \c second value is the
		"method" name as a string.

		\details
		Notice that if the $callee variable does not begin with a "frame specifier", it is assumed that the object
		belongs to the previous frame (e.g. the caller of the method). This holds true even if the method is actually
		defined in the root frame. For example \code function { obj.meth() } \endcode would trigger an error even if
		\c ::obj is defined since \c obj isn't defined in our function. While \code function { ::obj.meth() } \endcode
		works.

		One common use for this function is in a PikaScript object constructor for extracting the "this" reference
		that should be constructed. Another situation where this routine is useful is if you use the "elevate" function
		to aggregate various methods into a single C++ function. You may then use this function to extract the method
		name.
	*/
	static std::pair<Value, String> getThisAndMethod(Frame& frame);
	static Value getThis(Frame& frame) { return getThisAndMethod(frame).first; }										///< Returns only the "this" value as descripted in getThisAndMethod().
	static Value getMethod(Frame& frame) { return getThisAndMethod(frame).second; }										///< Returns only the "method" value as descripted in getThisAndMethod().

	struct lib {
		static Value elevate(Frame& frame);																				///< Used to "aggregate" different method calls into a single function call.
		static String character(double d);
		static bool deleter(const Frame& frame);
		static Value evaluate(const Frame& frame);
		static bool exists(const Frame& frame);
		static ulong find(const String& a, const String& b);
		static void foreach(Frame& frame);
		static String input(const String& prompt);
		static Value invoke(Frame& frame);
		static ulong length(const String& s);
		static String load(const String& file);
		static String lower(String s);
		static ulong mismatch(const String& a, const String& b);
		static uint ordinal(const String& s);
		static ulong parse(Frame& frame);
		static String precision(const Frame& frame);
		static void print(const String& s);
		static String radix(const Frame& frame);
		static double random(double m);
		static String reverse(String s);
		static void save(const String& file, const String& chars);
		static int system(const String& command);
		static ulong search(const String& a, const String& b);
		static ulong span(const String& a, const String& b);
		static void thrower(const String& s);
		static Value time(const Frame&);
		static void trace(const Frame& frame);
		static Value tryer(Frame& frame);
		static String upper(String s);
	};

	static void addLibraryNatives(Frame& frame, bool includeIO = true);													///< Registers the standard library native functions to \p frame. If \p includeIO is false, 'load', 'save', 'input', 'print' and 'system' will not be registered. Please, refer to the PikaScript standard library reference guide for more info on individual native functions.

}; // struct Script

/**
	StdConfig is a configuration class for Script that uses the reference implementations of STLValue and
	Script::STLVariables.
*/
struct StdConfig {
	typedef STLValue<std::string> Value;
	typedef Script<StdConfig>::STLVariables Locals;
	typedef Locals Globals;
};

/**
	This typedef exist for your convenience. If you wish to use the reference implementation of PikaScript you can now
	simply instantiate Pika::StdScript::FullRoot and off you go.
*/
typedef Script<StdConfig> StdScript;

#undef STR

} // namespace Pika

#endif
