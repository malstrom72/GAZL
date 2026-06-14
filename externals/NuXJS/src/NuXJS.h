/**
	NuXJS is released under the BSD 2-Clause License.

	Copyright (c) 2018-2025, Magnus Lidström

	Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
	following conditions are met:

	1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
	disclaimer.

	2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
	following disclaimer in the documentation and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
	INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
	INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
	THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
	IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**/

#ifndef NuXJS_h
#define NuXJS_h

#include "assert.h"
#include <algorithm>
#include <string>
#include <vector>
#include <iostream>
#include <ctime>
#include <limits>
#include <cstring>
#include <cstddef>
#include <stdint.h>

/**
	These global operator overloads makes it possible to allocate *anything* (and not only GCItems) on Heap. Just
	remember that (as opposed to GCItems) you need to call the overloaded delete operator explicitly, e.g.
	`::operator delete(pointer, associatedHeap).`

	Apparently these overloads must be declared before everything else or they will not be used (at least not by clang).
*/
namespace NuXJS { class Heap; }
void* operator new(size_t size, NuXJS::Heap* heap);
void* operator new[](size_t size, NuXJS::Heap* heap);
void operator delete(void* ptr, NuXJS::Heap* heap);
void operator delete[](void* ptr, NuXJS::Heap* heap);

namespace NuXJS {

typedef unsigned char Byte;
typedef short Int16;
typedef unsigned short UInt16;
typedef int Int32;
typedef unsigned int UInt32;
typedef UInt16 Char;
typedef Int32 CodeWord;

bool isNaN(double d);
bool isFinite(double d);

class GCList;
class Heap;

/**
	GCItem is the base class for anything tracked by the garbage collector. Instances are allocated from a Heap and
	linked into a GCList for mark and sweep.
**/
class GCItem {
	friend class GCList;
	friend class Heap;

	/**
		The overloaded `new` operator enforces allocation of GCItems on Heaps because:
		
		1) The item's memory will be accounted for in the total memory used by the Heap.
		2) Performance is improved by reusing recently deallocated memory through memory pools.
	*/
	public:
		static void* operator new(size_t n, Heap& heap);	///< Will store a secret pointer to Heap in allocated memory.
		static void operator delete(void* ptr);				///< Will use the secret pointer to delete from correct Heap.
		static void operator delete(void* ptr, Heap& heap);	///< C++ calls this (only) if constructor throws.
	
	protected:
		GCItem() throw() : _gcList(0) { _gcPrev = _gcNext = this; }
		GCItem(GCList& gcList) throw();
		GCItem(const GCItem& copy) throw();
		GCItem& operator=(const GCItem&) throw() { return *this; }
		Heap& gcGetHeap() const;
		virtual void gcMarkReferences(Heap&) const { assert(_gcReferenceMarkingComplete = true); }
		virtual ~GCItem();
		friend void gcMark(Heap& heap, const GCItem* item);

		GCList* _gcList;
		GCItem* _gcPrev;
		GCItem* _gcNext;
	
	#ifndef NDEBUG
		/// In debug target we check that sub-classes call gcMarkReferences() all the way up to the GCItem.
		mutable bool _gcReferenceMarkingComplete;
	#endif

	private:
		static void* operator new[](size_t, Heap* heap); // N/A
		static void operator delete[](void*); // N/A
};

/**
	GCList is a simple intrusive list used by the garbage collector. Lists are double linked and contain a dummy node to
	keep the implementation small.
**/
class GCList : public GCItem {
	friend class GCItem;
	friend class Heap;
	
	public:
		Heap& getHeap() const throw() { return heap; }
		bool owns(const GCItem* item) throw() { return item->_gcList == this; }
		void claim(GCItem* item) throw();
		void relinquish(GCItem* item) throw();
		void deleteAll() throw();

	protected:
		GCList(Heap& heap) : heap(heap), count(0) { }
		Heap& heap;
		UInt32 count;
};
inline Heap& GCItem::gcGetHeap() const { return _gcList->getHeap(); }
inline GCItem::GCItem(GCList& gcList) throw() : _gcList(0) { _gcPrev = _gcNext = this; gcList.claim(this); }
inline GCItem::~GCItem() { if (_gcList != 0) { _gcList->relinquish(this); } }

const int MAX_POOLED_SIZE = 1024;
const int POOL_SIZE_GRANULARITY = 16;
const UInt32 MAX_SINGLE_ALLOCATION_SIZE = 2147483648U;

/**
	Heap manages memory allocation for all GCItems. It keeps track of allocated blocks and owns the GC lists used during
	garbage collection.
**/
class Heap {
	public:
		Heap();
		GCList& managed() throw() { return *currentList; }
		GCList& roots() throw() { return rootList; }
		void* allocate(size_t size);					///< Notice that allocated memory is *not* automatically released when Heap is destroyed (unless it is indirectly freed via the deletion of all managed GCItems).
		void free(void* ptr);							///< Null pointer is not ok, and naturally you must not free an already freed pointer.
		void drain(); 									///< Frees pooled blocks. Suggestion: call before each gc to only hold on to as much memory as we required since last gc.
		UInt32 count() const { return allocatedCount; }
		size_t size() const { return allocatedSize; }
		size_t pooled() const { return pooledSize; }
		void gc();
		virtual ~Heap();
		friend void gcMark(Heap& heap, const GCItem* item);

	protected:
		virtual void* acquireMemory(size_t size);
		virtual void releaseMemory(void* ptr, size_t size);
		static int calcPoolIndex(size_t size);
		void* pools[MAX_POOLED_SIZE / POOL_SIZE_GRANULARITY];
		UInt32 allocatedCount;
		size_t allocatedSize;
		size_t pooledSize;
		GCList managedListA;
		GCList managedListB;
		GCList rootList;
		GCList* currentList;
		GCList* newList;

	private:
		Heap(const Heap& that); // N/A
		Heap& operator=(const Heap& that); // N/A
};

template<class ITERATOR> void gcMark(Heap& heap, ITERATOR begin, ITERATOR end) {
	for (ITERATOR it = begin; it != end; ++it) {
		gcMark(heap, *it);
	}
}

inline void gcMark(Heap& heap, const GCItem* item) {
	if (item != 0 && item->_gcList == heap.currentList) {
		heap.newList->claim(const_cast<GCItem*>(item));
	}
}

inline void GCItem::operator delete(void* ptr, Heap& heap) {
	::operator delete(reinterpret_cast<Heap**>(ptr) - 1, &heap);
}

inline void GCItem::operator delete(void* ptr) {
	Heap** p = reinterpret_cast<Heap**>(ptr) - 1;
	::operator delete(p, *p);
}

/**
	A simple STL-like vector template. Has internal buffer for small arrays (= fewer allocations on the heap). Grows
	automatically (like std::vector) with a factor of 1.5. You may designate a Heap to use for allocations
	(to properly account for the amount of memory in use).
*/
const UInt32 DEFAULT_INTERNAL_COUNT = 8;
template<typename T, UInt32 INTERNAL_COUNT = DEFAULT_INTERNAL_COUNT> class Vector {
	public:
		explicit Vector(Heap* optionalHeap)
				: associatedHeap(optionalHeap), allocated(0), count(0), elements(allocate(0)) {
		}

		Vector(UInt32 initialCount, Heap* optionalHeap)
				: associatedHeap(optionalHeap), allocated(initialCount), count(initialCount)
				, elements(allocate(initialCount)) {
			construct(initialCount, elements);
		}
		
		Vector(const T* b, const T* e, Heap* optionalHeap)
				: associatedHeap(optionalHeap), allocated(distance(b, e)), count(allocated)
				, elements(allocate(allocated)) {
			construct(allocated, elements, b);
		}

		Vector(const Vector& that)
				: associatedHeap(that.associatedHeap), allocated(that.count), count(that.count)
				, elements(allocate(that.count)) {
			construct(that.count, elements, that.elements);
		}

		void reserve(UInt32 requestedCapacity) {
			if (requestedCapacity > allocated) {
				reallocate(requestedCapacity, *this);
			}
		}

		void resize(UInt32 newCount) {
			if (newCount > allocated) {
				reallocate(std::max(newCount, allocated + allocated / 2), *this);
			}
			for (; count < newCount; ++count) {
				new (&elements[count]) T;
			}
			for (; count > newCount; --count) {
				elements[count - 1].~T();
			}
		}

		Vector& operator=(const Vector& that) {
			if (this != &that) {
				reallocate(that.count, that);
			}
			return *this;
		}

		void insert(T* p, const T* b, const T* e) {
			assert(begin() <= p && p <= end());
			assert(!(b <= p && p < e));	// can't insert from itself
			const UInt32 o = distance(begin(), p);
			const UInt32 n = distance(b, e);
			resize(count + n);
			std::copy_backward(begin() + o, end() - n, end());
			std::copy(b, e, begin() + o);
		}

		void erase(T* b, T* e) {
			assert(begin() <= b && e <= end());
			std::copy(e, end(), b);
			resize(count - distance(b, e));
		}

		Heap* getAssociatedHeap() const { return associatedHeap; }
		operator const T*() const { return elements; }
		operator T*() { return elements; }
		const T& operator[](ptrdiff_t i) const { assert(0 <= i); assert(static_cast<UInt32>(i) < count); return elements[i]; }
		T& operator[](ptrdiff_t i) { assert(0 <= i); assert(static_cast<UInt32>(i) < count); return elements[i]; }
		bool empty() const { return count == 0; }
		UInt32 size() const { return count; }
		UInt32 capacity() const { return allocated; }
		T* begin() const { return elements; }
		T* end() const { return elements + count; }
		T& back() { assert(count > 0); return (**this)[count - 1]; }
		const T& back() const { assert(count > 0); return (**this)[count - 1]; }
		void push(const T& t) { resize(count + 1); elements[count - 1] = t; }
		void pop(int n = 1) { resize(count - n); }
		void shrink() { if (count < allocated) { reallocate(count, *this); } }
		virtual ~Vector() { destruct(count, elements); }
	
	protected:
		static UInt32 distance(const T* b, const T* e) {
			const UInt32 n = static_cast<UInt32>(e - b);
			assert(n == e - b);
			return n;
		}

		T* allocate(UInt32 n) {
			return (n <= INTERNAL_COUNT ? reinterpret_cast<T*>(internal)
					: reinterpret_cast<T*>(::operator new(sizeof (T) * n, associatedHeap)));
		}

		void destruct(UInt32 n, T* a) throw() {
			for (; n > 0; --n) {
				a[n - 1].~T();
			}
			if (a != reinterpret_cast<T*>(internal)) {
				::operator delete(a, associatedHeap);
			}
		}

		void construct(UInt32 n, T* a) {
			UInt32 i = 0;
			try {
				for (; i < n; ++i) {
					new (&a[i]) T;
				}
			} catch (...) {
				destruct(i, a);
				throw;
			}
		}

		void construct(UInt32 n, T* a, const T* fromElements) {
			UInt32 i = 0;
			try {
				for (; i < n; ++i) {
					new (&a[i]) T(fromElements[i]);
				}
			} catch (...) {
				destruct(i, a);
				throw;
			}
		}

		void reallocate(UInt32 newCapacity, const Vector& source) {
			T* tempElements = allocate(newCapacity);
			if (tempElements != elements) {
				construct(source.count, tempElements, source.elements);
				std::swap(elements, tempElements);
				destruct(count, tempElements);
			} else if (this != &source) {
				assert(elements == reinterpret_cast<T*>(internal));
				destruct(count, elements);
				count = 0;
				construct(source.count, elements, source.elements);
			}
			count = source.count;
			allocated = newCapacity;
		}

		Heap* associatedHeap;
		UInt32 allocated;
		UInt32 count;
		T* elements;
		Byte internal[(INTERNAL_COUNT * sizeof (T) == 0 ? 1 : INTERNAL_COUNT * sizeof (T))];	// Zero-sized arrays are not ok
};

class String;
class Object;
class Function;
class Error;
class JSArray;

/**
	Value represents a generic JavaScript value. It can hold primitive types as well as object references and provides
	conversion and comparison helpers.
**/
class Value {
	friend class Table;
	
	public:
		Value()
		#ifndef NDEBUG
				: type(BAD_TYPE)
		#endif
				{ }
		Value(bool boolean) : type(BOOLEAN_TYPE) { var.boolean = boolean; }
		Value(Int32 number) : type(NUMBER_TYPE) { var.number = number; }
		Value(UInt32 number) : type(NUMBER_TYPE) { var.number = number; }
		Value(double number) : type(NUMBER_TYPE) { var.number = number; }
		Value(const String* string) : type(STRING_TYPE) { var.string = string; }
		Value(Object* object) : type(OBJECT_TYPE) { var.object = object; }

		bool isUndefined() const { return type == UNDEFINED_TYPE; }
		bool isNull() const { return type == NULL_TYPE; }
		bool isBoolean() const { return type == BOOLEAN_TYPE; }
		bool isNumber() const { return type == NUMBER_TYPE; }
		bool isString() const { return type == STRING_TYPE; }
		bool isObject() const { return type == OBJECT_TYPE; }
		Object* asObject() const { return (type == OBJECT_TYPE ? var.object : 0); }
		Function* asFunction() const;
		JSArray* asArray() const;
		Error* asError() const;
		const String* getString() const { assert(type == STRING_TYPE); return var.string; }
		Object* getObject() const { assert(type == OBJECT_TYPE); return var.object; }
		bool toBool() const;
		Int32 toInt() const;
		bool toArrayIndex(UInt32& index) const;				///< returns false if value is outside valid array index range (0..2^32-1)
		double toDouble() const;							///< Will *not* convert objects to numbers (as this would require running JS code).
		Function* toFunction(Heap& heap) const;
		const String* toString(Heap& heap) const; 			///< this toString() method does not run any script code, so it doesn't honor any user toString or valueOf implementations.
		std::wstring toWideString(Heap& heap) const;
		Object* toObjectOrNull(Heap& heap, bool requireExtensible) const;
		Object* toObject(Heap& heap, bool requireExtensible) const;
		const String* typeOfString() const;					///< Will return one of the following for standard types and objects: &UNDEFINED_STRING, &BOOLEAN_STRING, &NUMBER_STRING, &STRING_STRING, &OBJECT_STRING, &FUNCTION_STRING.
		bool isStrictlyEqualTo(const Value& r) const { return (type == r.type ? compareStrictly(r) : false); }	///< Same as JS operator ===
		bool isEqualTo(const Value& r) const;				///< Almost the same as JS operator == but will *not* convert objects to primitive values (as this would require running JS code).
		bool isLessThan(const Value& r) const;				///< Almost the same as JS operator < but will *not* convert objects to primitive values (as this would require running JS code).
		bool isLessThanOrEqualTo(const Value& r) const;		///< Almost the same as JS operator <= but will *not* convert objects to primitive values (as this would require running JS code).
		bool equalsString(const String& s) const;			///< Convenience routine for `v.isString() && v.getString()->isEqualTo(s)`.
		Value add(Heap& heap, const Value& r) const;		///< Almost the same as JS operator + but will *not* convert objects to primitive values (as this would require running JS code).
		friend void gcMark(Heap& heap, const Value& v);
		friend std::wostream& operator<<(std::wostream& out, const Value& v);

		static const Value UNDEFINED;
		static const Value NUL;					///< Sorry, NULL is a sensitive word, e.g. macro in Windows
		static const Value NOT_A_NUMBER;		///< Sorry, NAN is sensitive, defined by some math.h
		static const Value INFINITE_NUMBER;		///< Sorry, INFINITY is also sensitive
		static const Value NEG_INFINITE_NUMBER;

	protected:
		// Do NOT change order of elements in Type enum.
		enum Type {
			UNDEFINED_TYPE
			, NULL_TYPE
			, BOOLEAN_TYPE
			, NUMBER_TYPE
			, STRING_TYPE
			, OBJECT_TYPE
		#ifndef NDEBUG
			, BAD_TYPE = 0xBAADBAAD		// Only used when NDEBUG is not defined to detect use of uninitialized values.
		#endif
		} type;
		union Variant {
			bool boolean;
			double number;
			const String* string;
			Object* object;
		} var;

		explicit Value(Type type) throw() : type(type) { }
		explicit Value(Type type, const Variant& var) throw() : type(type), var(var) { }
		bool compareStrictly(const Value& y) const;
		static const String* TYPE_STRINGS[5];

	private:
		Value(const void* v); // N/A. Just to avoid unwanted casting of a pointer to bool.
};

typedef Byte Flags; // FIX : turn into enum
const Flags EXISTS_FLAG = 1; // FIX : turn these into enum, that's ok
const Flags READ_ONLY_FLAG = 2;
const Flags DONT_ENUM_FLAG = 4;
const Flags DONT_DELETE_FLAG = 8;
const Flags INDEX_TYPE_FLAG = 16;	///< internal index type, only used as an optimization for faster name -> local index lookup
const Flags STANDARD_FLAGS = EXISTS_FLAG;	///< use with setOwnProperty()
const Flags HIDDEN_CONST_FLAGS = READ_ONLY_FLAG | DONT_ENUM_FLAG | DONT_DELETE_FLAG | EXISTS_FLAG;
const Flags NONEXISTENT = 0;		///< use with getOwnProperty() to check for existence, e.g. getOwnProperty(o, k, v) != NONEXISTENT
const UInt32 TABLE_BUILT_IN_N = 3; ///< 1 << 3 == 8

/**
	Table implements a hash table for storing object properties. It provides fast lookup and is used internally by JS
	objects.
**/
class Table {
	public:
		/**
			The main reason why Bucket doesn't contain the Value class, but rather holds it's own Byte type and Variant
			union, is memory. This solution makes it possible to squeeze in key flags and a truncated 16-bit hash
			(for even quicker value lookup) in the same space as a Value.
		**/
		class Bucket {
			friend class Table;

			public:
				Bucket() : key(0) { };
				Value getValue() const {
					assert(valueExists() && (flags & INDEX_TYPE_FLAG) == 0);
					return Value(static_cast<Value::Type>(type), var);
				}
				Int32 getIndexValue() const {
					assert(valueExists() && (flags & INDEX_TYPE_FLAG) != 0);
					return index;
				}
				Flags getFlags() const { return flags; }
				const String* getKey() const { assert(keyExists()); return key; }
				bool doEnumerate() const { return ((flags & DONT_ENUM_FLAG) == 0); }
				bool hasStandardFlags() const { return flags == EXISTS_FLAG; }
				bool valueExists() const { return (key != 0 && ((flags & EXISTS_FLAG) != 0)); }

			protected:
				const String* key;
				Byte flags;
				Byte type;
				UInt16 hash16;
				union {
					Value::Variant var;
					Int32 index;
				};
				bool keyExists() const { return key != 0; }
		};

		Table(Heap* heap);
		Bucket* getFirst() const;											///< Returns first bucket with an existing value or 0.
		Bucket* getNext(Bucket* bucket) const;								///< Returns next bucket with an existing value or 0.
		const Bucket* lookup(const String* key) const;						///< Return bucket pointer for key or 0.
		Bucket* lookup(const String* key);									///< Return bucket pointer for key or 0.
		Bucket* insert(const String* key);									///< Insert key (if necessary) and return bucket pointer.
		bool update(Bucket* bucket, const Value& value, Flags flags = 0);	///< Update value. Returns false if bucket is marked as read-only.
		bool erase(Bucket* bucket);											///< Deletes bucket. Returns false if bucket is marked as dont-delete.
		UInt32 getLoadCount() const;										///< Returns number of used hash table entries (not necessarily the same as the number of existing buckets!).
		void update(Bucket* bucket, const Int32 index);						///< Update value as an index. Only used by name to index tables as an optimization.
		void gcMarkReferences(Heap& heap) const;
	
	protected:
		static UInt32 calcMaxLoad(UInt32 bucketCount);
		Bucket* find(const String* key, UInt32 hash);
		UInt32 rebuild(UInt32 newN);

		Vector<Bucket, 1U << TABLE_BUILT_IN_N> buckets;
		int bockets[123];
		UInt32 loadCount;													///< Count of buckets with defined keys.
};

class Enumerator;
class Processor;
class Runtime;

/**
	Object is the root for all scriptable objects. It provides the default implementations for property access and
	prototype handling.
**/
class Object : public GCItem {
	public:
		typedef GCItem super;
	
		virtual Function* asFunction();							///< Default returns 0. (Functions return `this`.)
		virtual JSArray* asArray();								///< Default returns 0. (Arrays return `this`.)
		virtual Error* asError();								///< Default returns 0. (Errors return `this`.)
		virtual const String* typeOfString() const;				///< Default returns "object". (Strings and functions override.)
		virtual const String* getClassName() const;				///< Default returns &O_BJECT_STRING ("Object"). Override if you implement custom native objects. Must return the same string pointer everytime.
		virtual const String* toString(Heap& heap) const; 		///< this toString() method does not run any script code, so it doesn't honor any user toString or valueOf implementations.
		virtual Value getInternalValue(Heap& heap) const;		///< Used by the standard library to retrieve internal value for wrappers (Number, String etc), source code for functions and parser function for RegExp. Default returns UNDEFINED_VALUE.
		virtual Object* getPrototype(Runtime& rt) const;		///< Default returns the Object prototype.

		virtual Flags getOwnProperty(Runtime& rt, const Value& key, Value* v) const;								///< Don't touch v if you return NONEXISTENT. Default returns NONEXISTENT.
		virtual bool setOwnProperty(Runtime& rt, const Value& key, const Value& v, Flags flags = STANDARD_FLAGS);	///< Insert a new or update an existing property. Return false if not possible (e.g. read-only property already exists). Default returns false.
		virtual bool updateOwnProperty(Runtime& rt, const Value& key, const Value& v);								///< Update existing property. Return false if it doesn't exist or can't be updated (e.g. read-only property exists). Can be overriden for optimization. (Default implementation checks existence with hasOwnProperty() first.)
		virtual bool deleteOwnProperty(Runtime& rt, const Value& key);												///< Default returns false.
		virtual Enumerator* getOwnPropertyEnumerator(Runtime& rt) const;											///< Default returns an empty enumerator.

		Flags getProperty(Runtime& rt, const Value& key, Value* v) const; 	///< Searches prototype chain.
		bool setProperty(Runtime& rt, const Value& key, const Value& v); 	///< First tries updateOwnProperty(). If that fails, checks prototype chain for read-only property with the same name and returns false if found. Otherwise attempts to insert a new property with setOwnProperty() and returns its outcome.
		bool isOwnPropertyEnumerable(Runtime& rt, const Value& key) const;
		bool hasOwnProperty(Runtime& rt, const Value& key) const; 			///< Checks via getOwnProperty().
		bool hasProperty(Runtime& rt, const Value& key) const;				///< Checks via getProperty().
		Enumerator* getPropertyEnumerator(Runtime& rt) const;				///< Unlike getOwnPropertyEnumerator() this one also enumerates all prototype properties.

	protected:
		Object() { }
		Object(GCList& gcList) : super(gcList) { }
};

inline Function* Value::asFunction() const { return (type == OBJECT_TYPE ? var.object->asFunction() : 0); }
inline JSArray* Value::asArray() const { return (type == OBJECT_TYPE ? var.object->asArray() : 0); }
inline Error* Value::asError() const { return (type == OBJECT_TYPE ? var.object->asError() : 0); }

/**
	Enumerators are Objects because it is convenient for the VM. They should never be accessable directly from JS.
**/
class Enumerator : public Object {
	public:
		typedef Object super;
		virtual const String* nextPropertyName() = 0;

	protected:
		Enumerator() { }
		Enumerator(GCList& gcList) : super(gcList) { }
};

/**
	RangeEnumerator iterates over a numeric range and returns each index as a string when requested. Used by
	`getOwnPropertyEnumerator()` of `String` and `JSArray`.
**/
class RangeEnumerator : public Enumerator {
	public:
		typedef Enumerator super;
		RangeEnumerator(GCList& gcList, Int32 from, Int32 count);
		virtual const String* nextPropertyName();
	
	protected:
		Heap& heap;
		const Int32 to;
		Int32 index;
};

/**
	JoiningEnumerator chains two enumerators, yielding all properties from one followed by another. Used to join
	property names in prototype chains and more.
**/
class JoiningEnumerator : public Enumerator {
	public:
		typedef Enumerator super;
		JoiningEnumerator(GCList& gcList, Runtime& rt, const Object* objectA, Enumerator* enumeratorB);
		virtual const String* nextPropertyName();

	protected:
		Runtime& rt;
		const Object* const objectA;
		Enumerator* const enumeratorA;
		Enumerator* const enumeratorB;

		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, objectA);
			gcMark(heap, enumeratorA);
			gcMark(heap, enumeratorB);
			super::gcMarkReferences(heap);
		}
};

/**
	Note that although the ECMAScript specification clearly separates string values from String objects, this
	implementation regards both as Objects for efficiency(e.g. for quickly retrieving properties like the character
	elements and "length").

	String is immutable and it will never contain any references to other objects. Therefore it is ok to not place them
	in a heap. (See GCItem for more info.)

	The ECMAScript String object on the other hand is represented by an extensible StringWrapper.
**/
class String : public Object {
	public:
		typedef Object super;

		static const String* allocate(Heap& heap, const char* s) { return new(heap) String(heap.managed(), s); }		///< Creates a (gc'able) string by copying a zero-terminated 8-bit string. (This function is equivalent to `new(heap) String(heap.managed(), s)`)
		static const String* concatenate(Heap& heap, const String& l, const String& r) {								///< Creates a (gc'able) string by concatenating two existing strings. Notice that it is ok to pass temporary String values for `l` and `r` since these will be used by value only. (This function is equivalent to `new(heap) String(heap.managed(), l, r)`)
			return new(heap) String(heap.managed(), l, r);
		}
		static const String* fromInt(Heap& heap, Int32 i);								///< Returns a (gc'able) string from an integer. For small integers (-1000..1000), a pointer to a constant string (created on startup) is returned. For larger integers, a new string is created on the heap every time.
		static const String* fromDouble(Heap& heap, double d);							///< Returns a (gc'able) string from a double value. Handles NaNs and infinites. For these special values and for small integers (-1000..1000), a pointer to a constant string (created on startup) is returned. For other values, a new string is created on the heap every time.

		String();																		///< Creates the empty string (allocated on standard C++ heap). Used for constant string generation on startup.
		String(const char* s);															///< Creates a new string by copying a zero-terminated 8-bit string (allocated on standard C++ heap, so it will not be gc'ed). Will also generate hash and bloom code. Use for global string constants.
		String(const wchar_t* s);														///< Creates a new string by copying a zero-terminated wchar_t string (allocated on standard C++ heap, so it will not be gc'ed). Will also generate hash and bloom code. Use for global string constants.
		String(const Char* b, const Char* e);											///< Creates a new string by copying from begin to end pointers (allocated on standard C++ heap, so it will not be gc'ed). Will also generate hash and bloom code. Used for constant string generation on startup.
		String(GCList& gcList, const char* s);											///< Creates a new string by copying a zero-terminated 8-bit string. Will also generate hash and bloom code.
		String(GCList& gcList, const wchar_t* s);										///< Creates a new string by copying a zero-terminated wchar_t string. Will also generate hash and bloom code.
		String(GCList& gcList, const char* b, const char* e);							///< Creates a new string by copying characters between two pointers.
		String(GCList& gcList, const Char* b, const Char* e);							///< Creates a new string by copying characters between two pointers.
		String(GCList& gcList, const String& l, const String& r);						///< Creates a new string by concatenating two strings. Notice that it is ok to pass temporary String values for `l` and `r` since these will be used by value only.
		String(GCList& gcList, const std::string& s);
		String(GCList& gcList, const std::wstring& s);									///< If wchar_t is 16-bit, this constructor assumes the wstring is already in UTF16 format and simply copies all characters. If it is 32-bit, it will be converted to UTF16 accordingly.
		virtual const String* typeOfString() const;
		virtual const String* getClassName() const;	// &S_TRING_STRING
		virtual const String* toString(Heap&) const { return this; }
		virtual Object* getPrototype(Runtime& rt) const;
		virtual Flags getOwnProperty(Runtime& rt, const Value& key, Value* v) const;
		virtual Enumerator* getOwnPropertyEnumerator(Runtime& rt) const;
		virtual Value getInternalValue(Heap&) const { return Value(this); }
		operator const Char*() const { return chars; }
		operator Char*() { return chars; }
		const Char& operator[](ptrdiff_t i) const { return chars[i]; }
		Char* begin() const { return chars.begin(); }
		Char* end() const { return chars.end(); }
		bool empty() const { return chars.empty(); }
		UInt32 size() const { return chars.size(); }
		UInt32 calcHash() const;
		bool isEqualTo(const String& o) const {
			return (this == &o || (size() == o.size() && std::equal(begin(), end(), o.begin())));
		}
		bool isLessThan(const String& o) const;
		std::string toUTF8String() const;												///< returns WTF-8 for host error/reporting paths
		std::wstring toWideString() const;												///< If wchar_t is 32-bit, UTF16 will be decoded automatically. Unmatched surrogate code units are copied through unchanged.
		UInt32 createBloomCode() const;

	protected:
		Vector<Char> chars;
		mutable UInt32 hash;
		mutable UInt32 bloom;
};

inline bool Value::equalsString(const String& s) const { return (type == STRING_TYPE && s.isEqualTo(*var.string)); }
inline std::wstring Value::toWideString(Heap& heap) const { return toString(heap)->toWideString(); }

/**
	StringListEnumerator enumerates over a list of strings provided at construction time. Used by
	`JSObject::getOwnPropertyEnumerator()`.
**/
class StringListEnumerator : public Enumerator {
	public:
		typedef Enumerator super;
		StringListEnumerator(GCList& gcList, UInt32 initialCapacity);
		void add(const String* s);
		virtual const String* nextPropertyName();

	protected:
		Vector<const String*> stringList;
		UInt32 nextIndex;

		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, stringList.begin(), stringList.end());
			super::gcMarkReferences(heap);
		}
};

// easier names for fundamental Value constants
extern const Value UNDEFINED_VALUE, NULL_VALUE, NAN_VALUE, INFINITY_VALUE, NEG_INFINITY_VALUE, FALSE_VALUE, TRUE_VALUE;

// class names (capitalized)
extern const String A_RGUMENTS_STRING, A_RRAY_STRING, B_OOLEAN_STRING, D_ATE_STRING, E_RROR_STRING, F_UNCTION_STRING
		, N_UMBER_STRING, O_BJECT_STRING, S_TRING_STRING;

// typeof names (all lowercase)
extern const String BOOLEAN_STRING, NUMBER_STRING, OBJECT_STRING, STRING_STRING, FUNCTION_STRING;

// other useful string constants (all lowercase)
extern const String EMPTY_STRING, LENGTH_STRING, UNDEFINED_STRING, NULL_STRING;


/**
	JSObject represents a standard extensible JavaScript object with its own property table and prototype pointer.
**/
class JSObject : public Object, public Table {
	public:
		typedef Object super;
	
		JSObject(GCList& gcList, Object* prototype);
		virtual Object* getPrototype(Runtime& rt) const;
		virtual Flags getOwnProperty(Runtime& rt, const Value& key, Value* v) const;
		virtual bool setOwnProperty(Runtime& rt, const Value& key, const Value& v, Flags flags = STANDARD_FLAGS);
		virtual bool updateOwnProperty(Runtime& rt, const Value& key, const Value& v);
		virtual bool deleteOwnProperty(Runtime& rt, const Value& key);
		virtual Enumerator* getOwnPropertyEnumerator(Runtime& rt) const;
	
	protected:
		Object* prototype;
		virtual void gcMarkReferences(Heap& heap) const {
			Table::gcMarkReferences(heap);
			gcMark(heap, prototype);
			super::gcMarkReferences(heap);
		}
};

/**
	LazyJSObjects become complete extensible "script objects" first when their properties are accessed. There are a
	number of reasons why this is desirable:
	
	1)	Performance: not having to setup a fully customizable object directly is benficial when it is rare that the
		user accesses properties or extends the object(but you still need an object reference). Example are Functions
		which are most often not treated as objects by the user.
	
	2) 	Memory: until the user accesses or adds properties, LazyJSObjects can be super tiny (vtable pointer + pointer
		to complete object + whatever internal fields are needed).
	
	3) 	Doesn't require a Runtime or even a Heap to be constructed. Although they are required to be placed on the
		heap since they contain a reference.
	
	This class is a template so this concept can be used with different super classes.
**/
template<class SUPER> class LazyJSObject : public SUPER {
	public:
		typedef SUPER super;
		LazyJSObject(GCList& gcList) : super(gcList), completeObject(0) { }
		virtual Flags getOwnProperty(Runtime& rt, const Value& key, Value* v) const;
		virtual bool setOwnProperty(Runtime& rt, const Value& key, const Value& v, Flags flags = STANDARD_FLAGS);
		virtual bool deleteOwnProperty(Runtime& rt, const Value& key);
		virtual Enumerator* getOwnPropertyEnumerator(Runtime& rt) const;

	protected:
		virtual void constructCompleteObject(Runtime& rt) const = 0;
		JSObject* getCompleteObject(Runtime& rt) const;
		mutable JSObject* completeObject;

		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, completeObject);
			super::gcMarkReferences(heap);
		}
};

/**
	JSArray implements the built in JavaScript array with support for a dense element vector and lazy object creation
	(for sparse arrays etc).
**/
class JSArray : public LazyJSObject<Object> {
	public:
		typedef LazyJSObject<Object> super;
	
		JSArray(GCList& gcList);
		JSArray(GCList& gcList, UInt32 initialLength);	// Will fill with UNDEFINED_VALUE. Just an optimization if you know the final array length beforehand.
		JSArray(GCList& gcList, UInt32 initialLength, const Value* initialElements);
		virtual const String* getClassName() const;	// &A_RRAY_STRING
		virtual JSArray* asArray();
		virtual Object* getPrototype(Runtime& rt) const;
		// FIX : toString too?
		virtual Flags getOwnProperty(Runtime& rt, const Value& key, Value* v) const;
		virtual bool setOwnProperty(Runtime& rt, const Value& key, const Value& v, Flags flags = STANDARD_FLAGS);
		virtual bool updateOwnProperty(Runtime& rt, const Value& key, const Value& v);
		virtual bool deleteOwnProperty(Runtime& rt, const Value& key);
		virtual Enumerator* getOwnPropertyEnumerator(Runtime& rt) const;
		void pushElements(Runtime& rt, Int32 count, const Value* elements);
		UInt32 getLength() const { return length; }	// fix: make virtual and have for all objects?
		bool updateLength(UInt32 newLength);	// fix: make virtual and have for all objects?
		Value getElement(Runtime& rt, UInt32 index) const;
		bool setElement(Runtime& rt, UInt32 index, const Value& v);

	protected:
		virtual void constructCompleteObject(Runtime& rt) const;
		void sliceDenseVector(Runtime& rt, const Value& key);
		bool setOwnPropertyInternal(Runtime& rt, const Value& key, const Value& v, Flags flags, bool& result);
		UInt32 length;
		Vector<Value> denseVector;
		virtual void gcMarkReferences(Heap& heap) const {
			const_cast<JSArray*>(this)->denseVector.shrink(); // FIX : split into two different gc-things? it really *is* different to just mark stuff and actively shrink / compress stuff
			gcMark(heap, denseVector.begin(), denseVector.end());
			super::gcMarkReferences(heap);
		}
};

/**
	Constants hold literal values shared between Code objects. This separation allows different functions to reuse the
	same constant pool as an optimization.
**/
class Constants : public GCItem, public Vector<Value> {
	public:
		typedef GCItem super;
		Constants(GCList& gcList) : super(gcList), Vector<Value>(&gcList.getHeap()) { }
	
	protected:
		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, begin(), end());
			super::gcMarkReferences(heap);
		}
};

/**
	SourceCodeUnit owns the original source string, the filename and a fast line-number lookup table.
**/
class SourceCodeUnit : public GCItem {
	public:
		typedef GCItem super;
		SourceCodeUnit(GCList& gcList, const String* sourceCode, const String* fileName);
		const String* getSource() const { assert(source != 0); return source; }
		const String* getFileName() const { assert(fileName != 0); return fileName; }
		void computeLineColumn(UInt32 offset, UInt32& line, UInt32& column) const;

	protected:
		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, source);
			gcMark(heap, fileName);
       		super::gcMarkReferences(heap);
		}

		const String* const source;
		const String* const fileName;
		Vector<UInt32> lineOffsets; 	// Byte offsets of line beginnings in source code. lineOffsets[0] is always 0.
};

/**
	Code represents compiled bytecode and associated metadata. It is an Object so that it can be stored as a constant
	and referenced by multiple functions.
	Source metadata such as filenames and line/column tables live on the associated `SourceCodeUnit`; full-script
	code objects keep their legacy `source` pointer in sync with the unit's source string.
**/
class Code : public Object {
	friend class FunctionScope;
	friend class Compiler; // FIX : maybe not one day?
	
	public:
		typedef Object super;
		struct SourceLocation {
			const String* fileName; 	// will not be 0
			const String* functionName; // can be 0 (if Code is not a function) or empty string (if anonymous function)
			UInt32 offset;
			UInt32 line;
			UInt32 column;
		};

		Code(GCList& gcList, Constants* sharedConstants, SourceCodeUnit* sourceUnit);
		bool lookupNameIndex(const String* name, Int32& index) const;
		UInt32 getVarsCount() const { return varNames.size(); }
		UInt32 getArgumentsCount() const { return argumentNames.size(); }
		const String* getLocalName(Int32 index) const { return (index < 0 ? varNames[static_cast<UInt32>(~index)] : argumentNames[static_cast<UInt32>(index)]); }
		const Constants* getConstants() const { return constants; }
		const CodeWord* getCodeWords() const { return codeWords.begin(); }
		UInt32 getCodeSize() const { return codeWords.size(); }
		const String* getName() const { return name; }		// can be 0 (which means Code is not a function) or empty string (which means anonymous function)
		const String* getSource() const { assert(source != 0); return source; }
		SourceCodeUnit* getSourceUnit() const { assert(sourceUnit != 0); return sourceUnit; }
		SourceLocation lookupSourceLocation(const CodeWord* instructionPointer) const;
		UInt32 getMaxStackDepth() const { return maxStackDepth; }
		UInt32 calcLocalsSize(UInt32 argc) const { return getVarsCount() + std::max(getArgumentsCount(), argc); }

	protected:
		Vector<CodeWord> codeWords;
		Constants* const constants;
		SourceCodeUnit* const sourceUnit;
		Vector<UInt32> codeOffsets;
		Vector<UInt32> sourceOffsets;
		Table nameIndexes;							///< < 0 : local variables, >= 0 : arguments, CATCH_PARAMETER == current catch parameter during compile-time only (don't use fast index binding)
		Vector<const String*> varNames;				///< Notice that this list is reversed in relation to indexes in the "locals" array in FunctionScope.
		Vector<const String*> argumentNames;
		const String* name;
		const String* selfName;
		const String* source;
		UInt32 bloomSet;							///< Bloom bits of all local variables, arguments (+ self name and "arguments"). For faster scope resolution.
		UInt32 maxStackDepth;

		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, constants);
			gcMark(heap, sourceUnit);
			nameIndexes.gcMarkReferences(heap);
			gcMark(heap, varNames.begin(), varNames.end());
			gcMark(heap, argumentNames.begin(), argumentNames.end());
			gcMark(heap, name);
			gcMark(heap, selfName);
			gcMark(heap, source);
			super::gcMarkReferences(heap);
		}
};

/**
	The Function base class, which can be used to interface with native C++ functions, does not reference other objects.
	Therefore it is ok to not place instances in a heap. (See GCItem for more info.)
**/
class Function : public Object {
	public:
		typedef Object super;
	
		virtual Function* asFunction();
		virtual const String* typeOfString() const;
		virtual const String* getClassName() const;	// &F_UNCTION_STRING
		virtual const String* toString(Heap& heap) const;
		virtual Value getInternalValue(Heap& heap) const;
		virtual Object* getPrototype(Runtime& rt) const;
		virtual Value construct(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject);
		virtual bool hasInstance(Runtime& rt, Object* object) const;
		virtual const Code* getScriptCode() const { return 0; }
		virtual Value invoke(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject = 0) = 0;

	protected:
		Function() { }
		Function(GCList& gcList) : super(gcList) { }
};

typedef Value (*NativeFunction)(Runtime&, Processor&, UInt32, const Value*, Object*);

// FIX : overkill?
template<class F> struct FunctorAdapter : public Function {
	typedef Function super;
	FunctorAdapter(const F& f) : f(f) { }
	FunctorAdapter(GCList& gcList, const F& f) : super(gcList), f(f) { }
	virtual Value invoke(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject) {
		return f(rt, processor, argc, argv, thisObject);
	}
	F f;
};

/**
	ExtensibleFunction forms the basis for user defined functions that behave like normal objects when accessed lazily.
**/
class ExtensibleFunction : public LazyJSObject<Function> {
	public:
		typedef LazyJSObject<Function> super;
		ExtensibleFunction(GCList& gcList) : super(gcList) { }

	protected:
		virtual void constructCompleteObject(Runtime&) const { }
		void createPrototypeObject(Runtime& rt, Object* forObject, bool dontEnumPrototype) const;
};

/**
	Scope represents a lexical environment chain used during execution. It stores local variables and links to a parent
	scope.
**/
class Scope : public GCItem {
	public:
		typedef GCItem super;
		Scope(GCList& gcList, Scope* parentScope);
		virtual Flags readVar(Runtime& rt, const String* name, Value* v) const;
		virtual void writeVar(Runtime& rt, const String* name, const Value& v);
		virtual bool deleteVar(Runtime& rt, const String* name);
		virtual void declareVar(Runtime& rt, const String* name, const Value& initValue, bool dontDelete);
		Value* getLocalsPointer() const { return localsPointer; }
		Scope* getParentScope() const { return parentScope; }
		void makeClosure() const;
		void leave() { if (deleteOnPop) { delete this; } }
		
	protected:
		Scope* const parentScope;
		Value* localsPointer; // Pointer is offset so that negative indexes addresses local variables and positive indexes addresses arguments.
		mutable bool deleteOnPop;
		
		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, parentScope);
			super::gcMarkReferences(heap);
		}
};

/**
	JSFunction encapsulates a piece of executable script with its associated scope closure.
**/
class JSFunction : public ExtensibleFunction {
	friend class FunctionScope;
	friend class Processor;

	public:
		typedef ExtensibleFunction super;
	
		JSFunction(GCList& gcList, const Code* code, Scope* closure);
		virtual const String* toString(Heap& heap) const;
		virtual Value getInternalValue(Heap& heap) const;
		virtual Value invoke(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject);
		virtual const Code* getScriptCode() const { return code; }

	protected:
		virtual void constructCompleteObject(Runtime& rt) const;
	
		const Code* const code;
		Scope* const closure;

		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, code);
			gcMark(heap, closure);
			super::gcMarkReferences(heap);
		}
};

enum ErrorType {
	GENERIC_ERROR, EVAL_ERROR, RANGE_ERROR, REFERENCE_ERROR, SYNTAX_ERROR, TYPE_ERROR, URI_ERROR, ERROR_TYPE_COUNT
};

/**
	Error represents the standard JavaScript Error objects and stores the error type together with optional name and
	message.
**/
class Error : public LazyJSObject<Object> {
	public:
		typedef LazyJSObject<Object> super;
		Error(GCList& heap, ErrorType type, const String* message = 0);
		virtual const String* getClassName() const { return &E_RROR_STRING; }
		virtual Error* asError() { return this; }
		virtual const String* toString(Heap& heap) const;
		virtual Value getInternalValue(Heap& heap) const; // error type name
		virtual Object* getPrototype(Runtime& rt) const;
		virtual bool setOwnProperty(Runtime& rt, const Value& key, const Value& v, Flags flags = STANDARD_FLAGS);
		virtual bool deleteOwnProperty(Runtime& rt, const Value& key);
		ErrorType getErrorType() const { return errorType; }
		const String* getErrorName() const { assert(name != 0); return name; }	// never 0
		const String* getErrorMessage() const { return message; };	// can be 0
		const String* getStackString() const { return stack; }	// can be 0
	
	protected:
		virtual void constructCompleteObject(Runtime& rt) const;
		void updateReflection(Runtime& rt);

		const ErrorType errorType;
		const String* name; 	// may get updated by script code
		const String* message; 	// may get updated by script code
		const String* stack; 		// may get updated by script code
		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, name);
			gcMark(heap, message);
			gcMark(heap, stack);
			super::gcMarkReferences(heap);
		}
};

class FunctionScope;
class Arguments : public LazyJSObject<Object> {
	public:
		typedef LazyJSObject<Object> super;

        Arguments(GCList& gcList, const FunctionScope* scope, UInt32 argumentsCount);
		virtual const String* getClassName() const;	// &A_RGUMENTS_STRING
		virtual const String* toString(Heap& heap) const;
		virtual Object* getPrototype(Runtime& rt) const;
		virtual Flags getOwnProperty(Runtime& rt, const Value& key, Value* v) const;
		virtual bool setOwnProperty(Runtime& rt, const Value& key, const Value& v, Flags flags = STANDARD_FLAGS);
		virtual bool deleteOwnProperty(Runtime& rt, const Value& key);
		virtual Enumerator* getOwnPropertyEnumerator(Runtime& rt) const;
		void detach();	// Arguments can get "detached" from FunctionScopes to prevent holding on to closures unnecessarily.
		virtual ~Arguments();	// At heap cleanup for example, Arguments might be destructed before the FunctionScope that "owns" it.

	protected:
		virtual void constructCompleteObject(Runtime& rt) const;
        Value* findProperty(const Value& key) const;
        const FunctionScope* scope;
		JSFunction* const function;
		UInt32 const argumentsCount;
		Vector<Byte> deletedArguments;
		Vector<Value> values;	// Contains copied values after the Argument has been detached from its closure.

		/**
			Notice that we do not mark the scope reference, thus creating a "weak" reference that is handled by
			FunctionScope's destructor
		**/
		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, values.begin(), values.end());
			gcMark(heap, function);
			super::gcMarkReferences(heap);
		}
};

/**
	FunctionScope manages local variables for an executing function and provides fast access during interpretation.
**/
class FunctionScope : public Scope {
	friend class Arguments;
	
	public:
		typedef Scope super;

		FunctionScope(GCList& gcList, JSFunction* function, UInt32 argc, const Value* argv);
		virtual Flags readVar(Runtime& rt, const String* name, Value* v) const;
		virtual void writeVar(Runtime& rt, const String* name, const Value& v);
		virtual bool deleteVar(Runtime& rt, const String* name);
		virtual void declareVar(Runtime& rt, const String* name, const Value& initValue, bool dontDelete);
		JSObject* getDynamicVars(Runtime& rt) const;
	   	virtual ~FunctionScope();	// At destruction we detach any created Arguments object (copying all values and severing the connection to the FunctionScope, in order to prevent "memory leaks".)

	protected:
		JSFunction* const function;
		const UInt32 passedArgumentsCount;
		Vector<Value> locals; // Includes variables and arguments.
		mutable JSObject* dynamicVars;
		mutable Arguments* arguments;
		UInt32 bloomSet;							///< Bloom bits of all local variables, arguments (+ self name and "arguments"). For faster scope resolution.
		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, function);
			gcMark(heap, locals.begin(), locals.end());
			gcMark(heap, dynamicVars);
			gcMark(heap, arguments);
			super::gcMarkReferences(heap);
		}
};

class Var;
const UInt32 STANDARD_JS_STACK_SIZE = 1024;				///< Max virtual processor stack size, should be enough for normal applications.
const UInt32 STANDARD_CYCLES_BETWEEN_AUTO_GC = 1024;	///< Number of virtual processor cycles between each check for GC.
const UInt32 MAX_CROSS_CALL_RECURSION = 64;				///< Max number of recursive calls from C++ to JS to prevent C++ stack overflow.
const Int32 CHECK_TIMEOUT_INTERVAL = 1024;				///< Minimum number of auto-gc cycles between each timeout check against clock-on-the-wall.
const Int32 MAX_PROTOTYPE_CHAIN_LENGTH = 32;			///< Limit prototype chain depth. Too deep chain could cause C++ stack overflow.
const size_t AUTO_GC_GROWTH_FACTOR = 2;
const size_t AUTO_GC_MIN_SIZE = 8192;
const size_t STRING_CONSTANTS_CACHE_SIZE_N = 8;			///< Size is 2^n
const size_t STRING_CONSTANTS_CACHE_SIZE = (1 << STRING_CONSTANTS_CACHE_SIZE_N);
const size_t STRING_CONSTANTS_CACHE_MAX_LENGTH = 64;	///< Max length of a string to cache.
const size_t MAX_MEMORY_CAP = std::numeric_limits<std::size_t>::max();

/**
	Runtime bundles together the heap, global objects and execution state required to run JavaScript code.
**/
class Runtime : public GCItem {
	friend class Processor;
	friend struct Support;
	
	protected:
		struct GlobalScope : public Scope {
			typedef Scope super;
			GlobalScope(GCList& gcList);
			virtual Flags readVar(Runtime& rt, const String* name, Value* v) const;
			virtual void writeVar(Runtime& rt, const String* name, const Value& v);
			virtual bool deleteVar(Runtime& rt, const String* name);
			virtual void declareVar(Runtime& rt, const String* name, const Value& initValue, bool dontDelete);
		};
	
		struct ErrorPrototype;
		struct FunctionPrototypeFunction;

	public:
		typedef GCItem super;
	
		enum PrototypeId {
			OBJECT_PROTOTYPE, FUNCTION_PROTOTYPE, STRING_PROTOTYPE, BOOLEAN_PROTOTYPE, NUMBER_PROTOTYPE
			, DATE_PROTOTYPE, ARRAY_PROTOTYPE, FIRST_ERROR_PROTOTYPE, PROTOTYPE_COUNT = FIRST_ERROR_PROTOTYPE + ERROR_TYPE_COUNT
			, ARBITRARY_PROTOTYPE 
		};

		Runtime(Heap& heap);

		void setGlobalObject(Object* newGlobals);		///< Assign a new global object. By default the global object is a standard JSObject.
		void setStackSize(UInt32 maxValuesOnStack);		///< Sets the stack size used by the Processor constructor. Default stack size is 1024 elements (see above) which should be sufficient for normal use.
		void setMemoryCap(size_t maxBytesUsed);			///< Sets an absolute limit on memory allocated by this run-time. Checked after each GC. If more memory than this is in use an exception is thrown.
		void resetTimeOut(Int32 timeOutSeconds);		///< Sets a time-out after which an exception is thrown (by checkTimeOut() which is called from runUntilReturn()).
		void noTimeOut();								///< Disables the time-out (default).
		void setupStandardLibrary();					///< Sets up all the standard library functions and support routines. It is possible to execute script code without calling setupStandardLibrary() but apart from missing all built-in "native objects" defined in EcmaScript there will be some other limitations (e.g. no support for regular expression literals and no support for implicit valueOf() / toString() conversions).

		Heap& getHeap() const { return heap; }
		Object* getObjectPrototype() const { return prototypes[OBJECT_PROTOTYPE]; }
		Object* getPrototypeObject(PrototypeId prototype) const;
		Object* getErrorPrototype(ErrorType error) const;
		Object* getGlobalObject() const { assert(globalObject != 0); return globalObject; }
		Runtime::GlobalScope* getGlobalScope() { return &globalScope; }
		JSObject* newJSObject() const; 							///< Convenience routine for `new(heap) JSObject(heap.managed(), rt.getObjectPrototype())`
		JSArray* newJSArray(UInt32 initialLength = 0) const;	///< Convenience routine for `new(heap) JSArray(heap.managed(), initialLength)`
		const String* newStringConstant(const char* s);

		Code* compileEvalCode(const String* expression);
		Code* compileGlobalCode(const String& source, const String* filename = 0);

		Var getGlobalsVar();							///< Convenience routine for `Var(rt, rt.getGlobalObject())`
		Var newObjectVar();								///< Convenience routine for `Var(rt, rt.newJSObject())`
		Var newArrayVar(UInt32 initialLength = 0);		///< Convenience routine for `Var(rt, rt.newJSArray())`

		Var call(Function* function, UInt32 argc, const Value* argv, Object* thisObject = 0);	///< Synchronous blocking JS function call. Throws if recursion depth becomes greater than MAX_CROSS_CALL_RECURSION (too prevent potential C++ stack overflows).
		Var eval(const String& expression);
		void run(const String& source, const String* filename = 0);

		void checkTimeOut();
		void autoGC(bool checkOutOfMemory);

		virtual Var runUntilReturn(Processor& processor);
		virtual double getCurrentEpochTime();			///< Get current utc time in milliseconds relative to Unix epoch (i.e. 0 is 1970-01-01T00:00:00Z). Override to implement higher resolution than standard C time() (whole seconds).

	protected:
		const String* newStringConstantWithHash(UInt32 hash, const char* s);
		void fetchFunction(const Object* supportObject, const char* name, Function** f);
	
		Heap& heap;
		GlobalScope globalScope;
		Object* globalObject;
		UInt32 stackSize;
		UInt32 callNestCounter;
		UInt32 checkTimeOutCounter;
		clock_t timeOut;
		size_t memoryCap;
		size_t gcThreshold;
		mutable Table evalCodeCache; ///< eval() has a cache of source code strings -> functions (as they are neutral to which closure they run in). It is emptied on each big gc sweep.
		Object* prototypes[PROTOTYPE_COUNT];

		Function* toPrimitiveFunctions[3]; ///< no preference, number, string
		Function* createRegExpFunction;
		Function* evalFunction;
		double unixEpochTimeDiff;

		mutable const String* stringConstantsCache[STRING_CONSTANTS_CACHE_SIZE];
	
	public:
		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, globalObject);
			gcMark(heap, prototypes + 0, prototypes + PROTOTYPE_COUNT);
			gcMark(heap, toPrimitiveFunctions + 0, toPrimitiveFunctions + 3);
			gcMark(heap, createRegExpFunction);
			gcMark(heap, evalFunction);
			// Yeah we just empty caches on each gc sweep
			std::fill(stringConstantsCache + 0, stringConstantsCache + STRING_CONSTANTS_CACHE_SIZE, (const String*)(0));
			evalCodeCache = Table(&heap);
			evalCodeCache.gcMarkReferences(heap);
			super::gcMarkReferences(heap);
		}
};
inline const String* Runtime::newStringConstant(const char* s) {
	const intptr_t pv = reinterpret_cast<intptr_t>(s);
	UInt32 hash = 0;
	for (size_t b = 0; b < sizeof (pv) * 8; b += STRING_CONSTANTS_CACHE_SIZE_N) {
		hash ^= ((pv >> b) & (STRING_CONSTANTS_CACHE_SIZE - 1));
	}
	return newStringConstantWithHash(hash, s);
}

struct Exception : public std::exception { virtual ~Exception() throw() { } };

struct ConstStringException : public Exception {
	typedef Exception super;
	ConstStringException(const char* s) throw() : stringPointer(s) { }
	virtual const char* what() const throw() { return stringPointer; }
	virtual ~ConstStringException() throw() { }
	const char* stringPointer;
};

struct ScriptException : public Exception {
	typedef Exception super;
	static void throwError(Heap& heap, ErrorType type, const String* message = 0);
	static void throwError(Heap& heap, ErrorType type, const char* message);
	ScriptException(Heap& heap, const Value& value) throw();
	virtual const char* what() const throw() { return utf8String.c_str(); }
	Error* asErrorObject() const { return value.asError(); }
	const char* getStackTrace() const;
	virtual ~ScriptException() throw() { }

	Value value;
	std::string utf8String;
	mutable std::string stackTrace;
};

class VarList;
class Property;
class Iterator;
typedef Var (*VarFunction)(Runtime& rt, const Var& thisObject, const VarList& args);

/**
	AccessorBase is an internal-only base class for Var and Property. It exposes the common read/call interface used by
	both helpers.
**/
class AccessorBase {
	friend class Var;
	friend class Property;
	
	public:
		class const_iterator;

	public:
		operator Value() const { return get(); }
		operator double() const { return get().toDouble(); }
		operator Object*() const { return get().toObject(rt.getHeap(), true); }
		operator Function*() const { return get().toFunction(rt.getHeap()); }
		operator std::wstring() const { return get().toWideString(rt.getHeap()); }
		operator const String*() const { return get().toString(rt.getHeap()); }
		operator const String&() const { return *get().toString(rt.getHeap()); }
		operator Var() const;
		template<typename T> T to() const { return static_cast<T>(*this); }
		Var operator()() const;
		template<typename T0> Var operator()(const T0& arg0) const;
		template<typename T0, typename T1> Var operator()(const T0& arg0, const T1& arg1) const;
		template<typename T0, typename T1, typename T2> Var operator()(const T0& arg0, const T1& arg1, const T2& arg2) const;
		Var operator()(const VarList& args) const;
		Var apply(Object* thisObject) const;
		Var apply(Object* thisObject, const VarList& args) const;
		template<typename T0> Var apply(Object* thisObject, const T0& arg0) const;
		template<typename T0, typename T1> Var apply(Object* thisObject, const T0& arg0, const T1& arg1) const;
		template<typename T0, typename T1, typename T2> Var apply(Object* thisObject, const T0& arg0, const T1& arg1, const T2& arg2) const;
		template<typename T> const Property operator[](const T& key) const;
		template<typename T> bool operator==(const T& t) const { return get().isStrictlyEqualTo(makeValue(t)); }
		template<typename T> bool operator!=(const T& t) const { return !get().isStrictlyEqualTo(makeValue(t)); }
		template<typename T> bool operator<(const T& t) const { return get().isLessThan(makeValue(t)); }
		template<typename T> bool operator<=(const T& t) const { return get().isLessThanOrEqualTo(makeValue(t)); }
		template<typename T> bool operator>(const T& t) const { return t < (*this); }
		template<typename T> bool operator>=(const T& t) const { return t <= (*this); }
		Var typeOf() const;
		Var className() const;
		bool instanceOf(const Function* f) const { return f->hasInstance(rt, get().asObject()); }
		template<typename T> bool has(const T& key) const { return get().toObject(rt.getHeap(), false)->hasProperty(rt, makeValue(key)); }
		template<typename T> bool equals(const T& t) const { return get().isEqualTo(makeValue(t)); }
		friend Var operator+(const AccessorBase& l, const AccessorBase& r);
		template<typename T> friend Var operator+(const AccessorBase& l, const T& r);
		UInt32 size() const;
		const_iterator begin() const;
		const_iterator end() const;

	protected:
		template<class F> struct VarFunctorAdapter;
		template<class F> struct VarMemberFunctionAdapter;
		AccessorBase(Runtime& rt) : rt(rt) { }
		virtual Value get() const = 0;
		virtual Var call(int argc, const Value* argv) const = 0;
		Value makeValue(const Value& v) const { return v; }
		Value makeValue(const AccessorBase& v) const;
		Value makeValue(const char* s) const { return rt.newStringConstant(s); }
		Value makeValue(const std::string& s) const { return new(rt.getHeap()) String(rt.getHeap().managed(), s); }
		Value makeValue(const std::wstring& s) const { return new(rt.getHeap()) String(rt.getHeap().managed(), s); }
		Value makeValue(const NativeFunction& f) const { return new(rt.getHeap()) FunctorAdapter<NativeFunction>(rt.getHeap().managed(), f); }
		Value makeValue(const VarFunction& f) const;
		template<class C> Value makeValue(Var (C::*const& cppMethod)(Runtime& rt, const Var& thisObject, const VarList& args)) const;
		Runtime& rt;
};
template<> inline bool AccessorBase::to<bool>() const { return get().toBool(); }	// operator bool() is ambiguous and notoriously dangerous so we left it out. Use var.to<bool>() instead.
template<> inline Int32 AccessorBase::to<Int32>() const { return get().toInt(); }	// Adding an operator int() would cause ambiguity with implicit casts, but to<Int32> is still a good idea.
template<> inline UInt32 AccessorBase::to<UInt32>() const { return static_cast<UInt32>(get().toInt()); }	// Adding an operator unsigned int() would cause ambiguity with implicit casts, but to<UInt32> is still a good idea.
template<> inline Value AccessorBase::to<Value>() const { return get(); } 			// to<Value> ends up ambigious without this.

/**
	Var is a garbage collected wrapper that exposes convenient C++ access to JavaScript values.
**/
class Var : public GCItem, public AccessorBase {
	public:
		typedef GCItem super;
		explicit Var(Runtime& rt) : super(rt.getHeap().roots()), AccessorBase(rt), v(UNDEFINED_VALUE) { }
		explicit Var(Runtime& rt, const Var& that) : super(rt.getHeap().roots()), AccessorBase(rt), v(makeValue(that)) { }
		template<class C> Var(Runtime& rt, C* cppObject, Var (C::*const& cppMethod)(Runtime& rt, const Var& thisObject, const VarList& args));
		template<typename T> Var(Runtime& rt, const T& t) : super(rt.getHeap().roots()), AccessorBase(rt), v(makeValue(t)) { }
		Var& operator=(const Var& that) { this->v = makeValue(that); return *this; }
		template<typename T> Var& operator=(const T& t) { this->v = makeValue(t); return *this; }
		template<typename T> Var& operator+=(const T& r) { this->v = get().add(rt.getHeap(), makeValue(r)); return *this; }

	protected:
		Value v;
		virtual Value get() const { return v; }
		virtual Var call(int argc, const Value* argv) const { return rt.call(*this, argc, argv); }
		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, v);
			super::gcMarkReferences(heap);
		}
};

/**
	Property is a helper that provides array-like syntax to access or update object properties through Var.
**/
class Property : public AccessorBase {
	friend class AccessorBase;
	
	public:
		template<typename T> const Property& operator=(const T& v) const { object->setProperty(rt, key, Var(rt, v)); return *this; }
		template<typename T> const Property& operator+=(const T& r) const { object->setProperty(rt, key, get().add(rt.getHeap(), makeValue(r))); return *this; }

	protected:
		typedef AccessorBase super;
		Property(Runtime& rt, Object* object, const Var& key) : super(rt), object(object), key(key) { }
		virtual Value get() const { Value v(UNDEFINED_VALUE); object->getProperty(rt, key, &v); return v; }
		virtual Var call(int argc, const Value* argv) const { return rt.call(*this, argc, argv, object); }
		Object* const object;
		const Var key;
};

/**
	VarList is a convenience container for passing multiple arguments between C++ and JavaScript.
**/
class VarList : public GCItem, public Vector<Value> {
	public:
		typedef GCItem super;
		VarList(Runtime& rt, UInt32 initialCount = 0)
			: super(rt.getHeap().roots()), rt(rt), Vector<Value>(initialCount, &rt.getHeap()) { }
		VarList(Runtime& rt, const Value& a0)
			: super(rt.getHeap().roots()), rt(rt), Vector<Value>(1, &rt.getHeap()) { begin()[0] = a0; }
		VarList(Runtime& rt, const Value& a0, const Value& a1)
			: super(rt.getHeap().roots()), rt(rt), Vector<Value>(2, &rt.getHeap()) { const Value v[] = { a0, a1 }; std::copy(v, v + 2, begin()); }
		VarList(Runtime& rt, const Value& a0, const Value& a1, const Value& a2)
			: super(rt.getHeap().roots()), rt(rt), Vector<Value>(3, &rt.getHeap()) { const Value v[] = { a0, a1, a2 }; std::copy(v, v + 3, begin()); }
		template<typename T> VarList(Runtime& rt, UInt32 count, const T* values)
			: super(rt.getHeap().roots()), rt(rt), Vector<Value>(count, &rt.getHeap()) { std::copy(values, values + count, begin()); }
		template<typename T> explicit VarList(Runtime& rt, const std::vector<T>& container)
			: super(rt.getHeap().roots()), rt(rt), Vector<Value>(container.data(), container.data() + container.size(), &rt.getHeap()) { } // Use with std::vector or C++11 std::array
		Value& operator[](ptrdiff_t index) { return Vector<Value>::operator[](index); }
		Var operator[](ptrdiff_t index) const { return Var(rt, (static_cast<size_t>(index) < size() ? *(begin() + index) : UNDEFINED_VALUE)); }

	protected:
		Runtime& rt;
		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, begin(), end());
			super::gcMarkReferences(heap);
		}
};

/**
	const_iterator allows range-based iteration over properties returned by an Enumerator.
**/
class AccessorBase::const_iterator : public GCItem {
	friend class AccessorBase;
	public:
		typedef GCItem super;
		const_iterator& operator++() { currentProperty = enumerator->nextPropertyName(); return *this; }
		Var operator*() const { assert(currentProperty != 0); return Var(rt, currentProperty); }
		bool operator==(const const_iterator& that) const { return currentProperty == that.currentProperty; }
		bool operator!=(const const_iterator& that) const { return currentProperty != that.currentProperty; }

	protected:
		const_iterator(Runtime& rt, Enumerator* enumerator) : super(rt.getHeap().roots()), rt(rt)
				, enumerator(enumerator), currentProperty(0) { }
		Runtime& rt;
		Enumerator* enumerator;
		const String* currentProperty;
		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, enumerator);
			gcMark(heap, currentProperty);
			super::gcMarkReferences(heap);
		}
};

// FIX : why template?
template<class F> struct AccessorBase::VarFunctorAdapter : public ExtensibleFunction {
	typedef ExtensibleFunction super;
	VarFunctorAdapter(GCList& gcList, const F& f) : super(gcList), f(f) { }
	virtual Value invoke(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object* thisObject) {
		return f(rt, Var(rt, thisObject), VarList(rt, argc, argv));
	}
	const F f;
};

inline AccessorBase::operator Var() const { return Var(rt, get()); }
inline Var AccessorBase::operator()() const { return call(0, 0); }
inline Var AccessorBase::operator()(const VarList& args) const { return call(args.size(), args.begin()); }
inline Var AccessorBase::apply(Object* thisObject) const { return rt.call(*this, 0, 0, thisObject); }
inline Var AccessorBase::apply(Object* thisObject, const VarList& args) const { return rt.call(*this, args.size(), args.begin(), thisObject); }
template<typename T0> inline Var AccessorBase::apply(Object* thisObject, const T0& arg0) const {
	const Value argv[1] = { makeValue(arg0) };
	return rt.call(*this, 1, argv, thisObject);
}
template<typename T0, typename T1> inline Var AccessorBase::apply(Object* thisObject, const T0& arg0, const T1& arg1) const {
	const Value argv[2] = { makeValue(arg0), makeValue(arg1) };
	return rt.call(*this, 2, argv, thisObject);
}
template<typename T0, typename T1, typename T2> inline Var AccessorBase::apply(Object* thisObject, const T0& arg0, const T1& arg1, const T2& arg2) const {
		const Value argv[3] = { makeValue(arg0), makeValue(arg1), makeValue(arg2) };
		return rt.call(*this, 3, argv, thisObject);
}
inline AccessorBase::const_iterator AccessorBase::end() const { return const_iterator(rt, 0); }
inline Value AccessorBase::makeValue(const AccessorBase& v) const { return v.operator Value(); }
inline std::wostream& operator<<(std::wostream& out, const AccessorBase& v) { out << std::wstring(v); return out; }
inline Var operator+(const AccessorBase& l, const AccessorBase& r) { return Var(l.rt, l.get().add(l.rt.getHeap(), r.get())); }
inline Var AccessorBase::typeOf() const { return Var(rt, get().typeOfString()); }
inline Var AccessorBase::className() const { return Var(rt, get().toObject(rt.getHeap(), false)->getClassName()); }

inline Value AccessorBase::makeValue(const VarFunction& f) const {
	return new(rt.getHeap()) VarFunctorAdapter<VarFunction>(rt.getHeap().managed(), f);
}

// FIX : vvv I think there should be a getLength() in Object*
inline UInt32 AccessorBase::size() const {
	const JSArray* a = get().asArray();
	return (a != 0 ? a->getLength() : static_cast<Int32>(operator[](&LENGTH_STRING)));
}

inline AccessorBase::const_iterator AccessorBase::begin() const {
	return ++const_iterator(rt, get().toObject(rt.getHeap(), false)->getPropertyEnumerator(rt));
}

template<typename T0> Var AccessorBase::operator()(const T0& arg0) const {
	const Value argv[1] = { makeValue(arg0) };
	return call(1, argv);
}

template<typename T0, typename T1> Var AccessorBase::operator()(const T0& arg0, const T1& arg1) const {
	const Value argv[2] = { makeValue(arg0), makeValue(arg1) };
	return call(2, argv);
}

template<typename T0, typename T1, typename T2> Var AccessorBase::operator()
		(const T0& arg0, const T1& arg1, const T2& arg2) const {
	const Value argv[3] = { makeValue(arg0), makeValue(arg1), makeValue(arg2) };
	return call(3, argv);
}

template<typename T> const Property AccessorBase::operator[](const T& key) const {
	return Property(rt, get().toObject(rt.getHeap(), true), Var(rt, key));
}

template<typename T> Var operator+(const AccessorBase& l, const T& r) {
	return Var(l.rt, l.get().add(l.rt.getHeap(), l.makeValue(r)));
}

template<class C> struct AccessorBase::VarMemberFunctionAdapter : public ExtensibleFunction {
	typedef ExtensibleFunction super;
	VarMemberFunctionAdapter(GCList& gcList, Var (C::*const &cppMethod)(Runtime& rt, const Var& thisObject, const VarList& args))
			: super(gcList), cppMethod(cppMethod) { }
	virtual Value invoke(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object* thisObject) {
		C* me = reinterpret_cast<C*>(thisObject);
		if ((me->C::getClassName()) != (me->getClassName())) {
			ScriptException::throwError(rt.getHeap(), TYPE_ERROR, "Invalid class");
		}
		assert(dynamic_cast<const C*>(thisObject) != 0);
		return (me->*cppMethod)(rt, Var(rt, thisObject), VarList(rt, argc, argv));
	}
	Var (C::*cppMethod)(Runtime& rt, const Var& thisObject, const VarList& args);
};
template<class C> Value AccessorBase::makeValue(Var (C::*const &cppMethod)(Runtime& rt, const Var& thisObject, const VarList& args)) const {
	return new(rt.getHeap()) VarMemberFunctionAdapter<C>(rt.getHeap().managed(), cppMethod);
}

template<class C> struct BoundVarMemberFunctionAdapter : public ExtensibleFunction {
	typedef ExtensibleFunction super;
	BoundVarMemberFunctionAdapter(GCList& gcList, C* cppObject, Var (C::*const &cppMethod)(Runtime& rt, const Var& thisObject, const VarList& args))
			: super(gcList), cppObject(cppObject), cppMethod(cppMethod) { }
	virtual Value invoke(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object* thisObject) {
		return (cppObject->*cppMethod)(rt, Var(rt, thisObject), VarList(rt, argc, argv));
	}
	C* cppObject;
	Var (C::*cppMethod)(Runtime& rt, const Var& thisObject, const VarList& args);
};

template<class C> Var::Var(Runtime& rt, C* cppObject, Var (C::*const& cppMethod)(Runtime& rt, const Var& thisObject, const VarList& args))
		: super(rt.getHeap().roots()), AccessorBase(rt)
		, v(new(rt.getHeap()) BoundVarMemberFunctionAdapter<C>(rt.getHeap().managed(), cppObject, cppMethod)) { }

/**
	Processor is the virtual machine executing compiled bytecode for a specific Runtime.
**/
class Processor : public GCItem {
	friend class JSFunction;
	
	public:
		typedef GCItem super;
	
		enum Opcode {	// FIX : sort
			INVALID_OP = -1 // FIX : why?!
			, CONST_OP										// operand: const_index, stack: -> constant
			, READ_LOCAL_OP									// operand: local_index, stack: -> value
			, WRITE_LOCAL_OP								// operand: local_index, stack: value -> value
			, WRITE_LOCAL_POP_OP							// operand: local_index, stack: value ->
			, READ_NAMED_OP									// operand: const_index (name), stack: -> value
			, WRITE_NAMED_OP								// operand: const_index (name), stack: value -> value
			, WRITE_NAMED_POP_OP							// operand: const_index (name), stack: value ->
			, CHECK_OBJECT_COERCIBLE_OP						// stack: value -> value
			, CHECK_RESOLVE_PROPERTY_OP						// stack: object, name -> object, name	// check coercible, then resolve object
			, GET_PROPERTY_OP								// stack: object, name -> value
			, SET_PROPERTY_OP								// stack: object, name, value -> value
			, SET_PROPERTY_POP_OP							// stack: object, name, value ->
			, ADD_PROPERTY_OP								// operand: const_index (name), stack: object, value -> object
			, PUSH_ELEMENTS_OP								// operand: count, stack: object, count * elements ... -> object
			, OBJ_TO_PRIMITIVE_OP							// stack: value -> primitive_value (no preference)	// these three must be in this exact order
			, OBJ_TO_NUMBER_OP								// stack: value -> primitive_value (number preferred)
			, OBJ_TO_STRING_OP								// stack: value -> primitive_value (string preferred)
			, PRE_EQ_OP										// stack: value, value -> value, value				// if first *or* second is object (and not both) and the other is string or number, convert object to primitive value
			, INC_OP, DEC_OP								// stack: value -> number
			, ADD_OP, SUB_OP, MUL_OP, DIV_OP, MOD_OP		// stack: left_value, right_value -> result_value
			, OR_OP, XOR_OP, AND_OP							// stack: left_value, right_value -> result_value
			, SHL_OP, SHR_OP, USHR_OP						// stack: left_value, right_value -> result_value
			, PLUS_OP, MINUS_OP, INV_OP, NOT_OP				// stack: value -> value
			, X_EQ_OP, X_NEQ_OP, EQ_OP, NEQ_OP				// stack: left_value, right_value -> boolean
			, LT_OP, LEQ_OP, GT_OP, GEQ_OP					// stack: left_value, right_value -> boolean
			, JMP_OP, JSR_OP								// operand: offset
			, JT_OP, JF_OP									// operand: offset, stack: boolean ->
			, JT_OR_POP_OP, JF_OR_POP_OP					// operand: offset, stack: boolean ->				// only pops when not jumping, for logical and/or instructions
			, POP_OP										// operand: count, stack: ... ->
			// FIX : push back is a really bad name, it's really pop back and then push sort of
			, PUSH_BACK_OP									// operand: count, stack: ..., back -> back
			, REPUSH_OP										// operand: offset, stack: ..., value, ... -> ..., value, ..., value
			, SWAP_OP
			, REPUSH_2_OP									// stack: value_1, value_2 -> value_1, value_2, value_1, value_2	// used for duplicating property reference with assignment operators like += etc
			, POST_SHUFFLE_OP								// stack: object, name, value -> value, object, name, value			// used for special post inc/dec logic on properties (see code)
			, CALL_OP										// operand: n, stack: function, n * args -> return_value
			, CALL_METHOD_OP								// operand: n, stack: object, name, n * args -> return_value
			, CALL_EVAL_OP									// operand: n, stack: function, n * args -> return_value			// special eval call is required because of need to differentiate direct or indirect call to eval
			, NEW_OP										// operand: n, stack: constructor object, n * args -> new_object, return_value
			, NEW_RESULT_OP									// stack: new_object, return_value -> new_this value
			, NEW_OBJECT_OP									// stack: -> object
			, NEW_ARRAY_OP									// stack: -> array_object
			, NEW_REG_EXP_OP								// stack: pattern_string, flags_string -> regexp_object
			// FIX : should we have an optional pop argument (push back) to RETURN_OP?
			, RETURN_OP
			// FIX : maybe we should also have a RETURN_VOID_OP?
			, THIS_OP, VOID_OP								// stack: -> value
			, DELETE_OP										// stack: object, name -> boolean
			, DELETE_NAMED_OP								// operand: const_index (name) -> boolean
			, GEN_FUNC_OP									// operand: const_index (function), stack: -> function
			, DECLARE_OP									// operand: const_index (name), stack: function|undefined ->		// only used by eval code to declare vars and functions (non-eval code sets all this up on entry and using WRITE_LOCAL_OP).
			, CATCH_SCOPE_OP								// operand: const_index for exception variable name, stack: value ->
			, WITH_SCOPE_OP									// stack: object ->
			, POP_FRAME_OP
			, TRY_OP										// operand: catch_offset
			, TRIED_OP
			, THROW_OP										// stack: value ->
			, IN_OP											// stack: string, object -> boolean
			, INSTANCE_OF_OP								// stack: object, function -> boolean
			, TYPEOF_OP										// stack: value -> string
			, TYPEOF_NAMED_OP								// operand: const_index (name), stack: -> string
			, GET_ENUMERATOR_OP								// stack: object -> enumerator
			, NEXT_PROPERTY_OP								// operand: exit_loop_offset, stack: enumerator -> string (unless end of loop)
			, OP_COUNT
		};
	
		struct OpcodeInfo {
			enum { TERMINAL = 1, POP_OPERAND = 2, POP_ON_BRANCH = 4, NO_POP_ON_BRANCH = 8 };
			Opcode opcode;
			const char* mnemonic;
			Int32 stackUse;
			Byte flags;
		};

		static CodeWord packInstruction(const Opcode opcode, const Int32 operand);
		static std::pair<Opcode, Int32> unpackInstruction(const CodeWord codeWord);
		static const OpcodeInfo& getOpcodeInfo(const Opcode opcode);

	public:
		Processor(Runtime& rt);
		void invokeFunction(Function* f, Int32 argc, const Value* argv, Object* thisObject = 0);
		void enterGlobalCode(const Code* code);
		void enterEvalCode(const Code* code, bool local = false);
		void enterFunctionCode(JSFunction* func, UInt32 argc, const Value* argv, Object* thisObject = 0);
		void throwVirtualException(const Value& exception);
		void addStackTrace(const Value& exception) const;
		void error(ErrorType errorType, const String* message = 0);
		bool run(Int32 maxCycles);
		Value getResult() const;	// make sure you've called run() until it returns false before calling this

	protected:
		struct Frame : public GCItem {
			typedef GCItem super;
			Frame(GCList& gcList, const CodeWord* returnIP, const Code* code, Scope* scope, Object* thisObject
				, Frame* previousFrame) : super(gcList), returnIP(returnIP), code(code), scope(scope)
				, thisObject(thisObject), previousFrame(previousFrame)
			{
				assert(code != 0);
				assert(scope != 0);
			}
			const CodeWord* const returnIP;
			const Code* const code;
			Scope* const scope;
			Object* const thisObject;
			Frame* const previousFrame;
			virtual void gcMarkReferences(Heap& heap) const {
				gcMark(heap, code);
				gcMark(heap, scope);
				gcMark(heap, thisObject);
				gcMark(heap, previousFrame);
				super::gcMarkReferences(heap);
			}
		};

		struct Catcher : public GCItem {
			typedef GCItem super;
			Catcher(GCList& gcList, const CodeWord* ip, Value* sp, Frame* frame, Catcher* nextCatcher)
					: super(gcList), ip(ip), sp(sp), frame(frame), nextCatcher(nextCatcher) { }
			const CodeWord* const ip;
			Value* const sp;
			Frame* const frame;
			Catcher* const nextCatcher;
			virtual void gcMarkReferences(Heap& heap) const {
				gcMark(heap, frame);
				gcMark(heap, nextCatcher);
				super::gcMarkReferences(heap);
			}
		};

		struct EvalScope;
		struct CatchScope;
		struct WithScope;

		void enter(const Code* code, Scope* scope, Object* thisObject);
		void push(const Value& v);
		void pop(Int32 count);
		void pop2push1(const Value& v);
		void innerRun();
		Object* convertToObject(const Value& v, bool requireExtensible);
		Function* asFunction(const Value& v);
		void invokeFunction(Function* f, Int32 popCount, Int32 argc, Object* thisObject = 0); // FIX : rename, unnecessary with the same name as the public method
		void newOperation(const Int32 argc);
		void reset();
		void pushFrame(const Code* code, Scope* scope, Object* thisObject);
		void popFrame();
		void popCatcher();

		static const OpcodeInfo opcodeInfo[OP_COUNT];

		Runtime& rt;
		Heap& heap;
		Int32 cyclesLeft;
		Frame* currentFrame;
		Catcher* firstCatcher;
		const CodeWord* ip;		// 0 = finished (or nothing invoked), run() will return false.
		Value* sp;
		Vector<Value> stack;

		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, currentFrame);
			gcMark(heap, firstCatcher);
			gcMark(heap, stack.begin(), sp + 1);
			super::gcMarkReferences(heap);
		}
};

const Int32 INVALID_CODE_OFFSET = -0x7FFFFFFF;
const Int32 DEAD_CODE_STACK_DEPTH = -0x7FFFFFFF;
struct OperatorInfo;

/**
	Compiler translates JavaScript source code into bytecode stored in a Code object.
**/
class Compiler : public GCItem {
	public:
		typedef GCItem super;
	
		enum Precedence {
			GROUP_PREC, PROPERTY_PREC, NEW_PREC, FUNCTION_CALL_PREC, POST_INC_DEC_PREC, PREFIX_PREC
			, MUL_DIV_PREC, ADD_SUB_PREC, SHIFT_PREC, RELATIONAL_PREC, EQUALITY_PREC, BIT_AND_PREC
			, BIT_XOR_PREC, BIT_OR_PREC, LOGICAL_AND_PREC, LOGICAL_OR_PREC, CONDITIONAL_PREC, ASSIGN_PREC
			, COMMA_PREC, LOWEST_PREC
		};

		enum Target { FOR_GLOBAL, FOR_FUNCTION, FOR_EVAL };

		Compiler(GCList& gcList, Code* code, Target compileFor, int initialNestCounter = 0);
		const Char* compile(const Char* b, const Char* e);
		const Char* compileFunction(const Char* b, const Char* e, const String* functionName, const String* selfName); // FIX : messy, why do we have compileFor if we separate this anyhow? Maybe subclass Compiler instead?
		void compile(const String& source);
		void getStopPosition(UInt32& offset, UInt32& lineNumber, UInt32& columnNumber) const;

	protected:
		struct CodeSection {
			CodeSection(Heap& heap, Int32 initialStackDepth)
				: code(&heap), lastEmitted(Processor::INVALID_OP), initialStackDepth(initialStackDepth)
				, stackDepth(initialStackDepth), maxStackDepth(initialStackDepth)
				, codeOffsets(&heap), sourceOffsets(&heap)
		 	{
			}
			
			void emit(Processor::Opcode opcode, Int32 operand, UInt32 sourceOffset);
			void pushSourceMapping(UInt32 offset);
			UInt32 popSourceMapping();
			void exportSourceMapping(Vector<UInt32>& toCodeOffsets, Vector<UInt32>& toSourceOffsets
					, UInt32 codeBase) const;
			void insertSection(const CodeSection& section);
			bool inDeadCode() const { return stackDepth == DEAD_CODE_STACK_DEPTH; }
			Vector<CodeWord> code;
			Processor::Opcode lastEmitted;
			const Int32 initialStackDepth;
			Int32 stackDepth;
			Int32 maxStackDepth;
			Vector<UInt32> codeOffsets;
			Vector<UInt32> sourceOffsets;
		};

		struct BranchPoint {
			BranchPoint(Int32 codeOffset = INVALID_CODE_OFFSET, Int32 stackDepth = DEAD_CODE_STACK_DEPTH)
					: codeOffset(codeOffset), stackDepth(stackDepth) { }
			bool isValid() const { return codeOffset != INVALID_CODE_OFFSET; };
			bool inDeadCode() const { return stackDepth == DEAD_CODE_STACK_DEPTH; };
			Int32 codeOffset;
			Int32 stackDepth;
		};

		struct ExpressionResult;
		struct SemanticScope;

		static const String* newHashedString(Heap& heap, const Char* b, const Char* e);
		void error(ErrorType type, const char* message);
		void emit(Processor::Opcode opcode, Int32 operand = 0);
		CodeSection* changeSection(CodeSection* newOutputSection);
		UInt32 addConstant(const Value& constant);
		void emitWithConstant(Processor::Opcode opcode, const Value& constant);
		BranchPoint emitForwardBranch(Processor::Opcode opcode);
		void completeForwardBranch(const BranchPoint& point);
		void completeForwardBranches(const BranchPoint* begin, const BranchPoint* end);
		BranchPoint markBackwardBranch();
		void emitBackwardBranch(Processor::Opcode opcode, const BranchPoint& point);

		Processor::Opcode evalPopOpcode() const;
		ExpressionResult safeKeep(); ///< Used to store result into a new temporary local (e.g. used by return statement if there are finally blocks to call first). Good rule of thumb is that anything that needs to be preserved over statements need to be safe-kept (except the completion value of course).
		void returnSafeKept(const ExpressionResult& xr);
		ExpressionResult makeRValue(const ExpressionResult& xr, bool toPrimitive = false, Processor::Opcode toPrimitiveOp = Processor::OBJ_TO_NUMBER_OP); ///< Creates an r-value (i.e. pushed on value stack) out of a result.
		ExpressionResult makeAssignment(const ExpressionResult& xr); ///< Creates an assignment out of an l-value (throws if xr is not a valid l-value)
		ExpressionResult discard(const ExpressionResult& xr); ///< Discards result (e.g. popping it if it is on stack)

		void completeIteratorContinues(const SemanticScope* fromScope, const SemanticScope* untilScope);
		void completeIteratorBreaks(const SemanticScope* fromScope, const SemanticScope* untilScope);
		void completeBreaks(const SemanticScope* ofScope);

		ExpressionResult operand(const OperatorInfo& op);
		ExpressionResult functionCall(Processor::Opcode opcode);
		Value stringOrNumberConstant();
		ExpressionResult arrayInitialiser();
		ExpressionResult objectInitialiser();
		int parseOperator(Precedence precedence, int opCount, const OperatorInfo* opList);
		bool preOperate(ExpressionResult& xr, Precedence precedence);
		bool postOperate(ExpressionResult& xr, Precedence precedence);
		ExpressionResult expression(Precedence precedence);
		ExpressionResult rvalueExpression(Precedence precedence = LOWEST_PREC);
		void rvalueGroup();
		void functionDefinition(const String* functionName, const String* selfName); // selfName is only for named function expressions, should be 0 otherwise
		bool optionalExpression(ExpressionResult& xr, Precedence precedence);
		ExpressionResult declareIdentifier(const String* name, bool func);
		ExpressionResult varDeclaration();
		void varDeclarations();
		void block(SemanticScope* scope);
		void forInStatement(SemanticScope* emptyLabelScope, SemanticScope* scopeLabelsEnd
				, const CodeSection& iterationSection, const ExpressionResult& iterationXR);
		void forStatement(SemanticScope* emptyLabelScope, SemanticScope* scopeLabelsEnd);
		void forOrForInStatement(SemanticScope* currentScope, SemanticScope* scopeLabelsEnd);
		void varStatement();
		void functionStatement();
		void breakStatement(SemanticScope* currentScope);
		void continueStatement(SemanticScope* currentScope);
		void ifStatement(SemanticScope* currentScope);
		void whileStatement(SemanticScope* currentScope, SemanticScope* scopeLabelsEnd);
		void doWhileStatement(SemanticScope* currentScope, SemanticScope* scopeLabelsEnd);
		void returnStatement(SemanticScope* currentScope);
		void withStatement(SemanticScope* currentScope);
		void throwStatement();
		void tryStatement(SemanticScope* currentScope);
		void switchStatement(SemanticScope* currentScope);
		void statement(SemanticScope* firstScope, SemanticScope* scopeLabelsEnd);
		void statementList(SemanticScope* firstScope);
		bool token(const char* t, bool eatLeadingWhite);
		void expectToken(const char* t, bool eatLeadingWhite);
		bool eof() const { return p == e; }
		void semicolon(bool acceptLF = true);
		void white();
		bool whiteNoLF();
		Char* unescape(Char* buffer, const Char* e);
		Vector<Char, 64> parseIdentifier(bool limitLeadingChar);
		const String* identifier(bool required, bool allowKeywords);

		Heap& heap;
		Code* const code;
		const Target compilingFor;
		CodeSection setupSection; ///< function declarations (and vars in eval code), inserted at top of function when finalizing
		CodeSection mainSection;
		const Char* b;
		const Char* p;
		const Char* e;
		const Char* sourceUnitBase;
		CodeSection* currentSection;
		bool acceptInOperator;
		int withScopeCounter; // FIX : if we have a Context object instead as "this" we could create a new one with a simple flag for this instead of yucky counter
		int nestCounter;

		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, code);
			super::gcMarkReferences(heap);
		}
};

/**
	Extended ScriptException thrown by Runtime::compileGlobalCode that includes the filename, character offset, line and
	column where the compilation error occurred.
*/
struct CompilationError : public ScriptException {
	CompilationError(const ScriptException& sourceException, const String* filename, const Compiler& fromCompiler)
			: ScriptException(sourceException), filename(filename) {
		fromCompiler.getStopPosition(offset, lineNumber, columnNumber);
	}
	const String* filename;
	UInt32 offset;
	UInt32 lineNumber;
	UInt32 columnNumber;
};

} /* namespace NuXJS */

#endif
