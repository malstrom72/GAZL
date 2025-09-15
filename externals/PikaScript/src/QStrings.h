/**
	\file QStrings.h
	
	QStrings is a high performance string class inspired by std::string.
	
	QStrings implements a subset of std::string and can be used as a much optimized string implementation for
	PikaScript. It achieves its great performance by:
	
	1) Maintaining a separate memory pool for smaller string buffers instead of using the slower standard heap.
	2) Sharing string buffers not only for full strings, but even for substrings.
	
	\warning
	
	WARNING! The current implementation of the memory pool is *not* thread-safe due to unprotected use of shared global
	data. You must only use QStrings in single-threaded applications or in the case of a multi-threaded application you
	must only use QStrings from a single thread at a time!
	
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

#ifndef QStrings_h
#define QStrings_h

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <wchar.h>
#include <string>
#include <algorithm>
#include <limits>
#include "assert.h"

namespace QStrings {

/* --- Declaration --- */
/**
	Lightweight string using pooled, reference-counted buffers.
	Buffers may be shared by substrings and unshared on modification.
	Not thread-safe; confine to one thread.
**/
template<typename C, size_t PS = (64 - 12)> class QString {
	public:		enum { npos = 0x7FFFFFFF };
	public:		typedef size_t size_type;
	public:		typedef C value_type;
	public:		template<typename E, class Q> class _iterator;
	public:		typedef _iterator<C, QString<C, PS> > iterator;
	public:		typedef _iterator<const C, const QString<C, PS> > const_iterator;
	public:		class Buffer;
	public:		typedef std::reverse_iterator<iterator> reverse_iterator;
	public:		typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
	public:		static void deinit() throw();
	public:		template<size_t N> QString(const C (&s)[N]);
	public:		QString();
	public:		QString(const QString& copy);
	public:		QString(C* s);
	public:		QString(size_type n, C c);
	public:		QString(const C* b, size_type l, size_type extra = 0);
	public:		QString(const_iterator b, const_iterator e);
	public:		QString(const C* b, const C* e);
	public:		QString(const std::basic_string<C>& copy);
	public:		QString(const QString& copy, size_type offset, size_type count);
	public:		QString& operator=(const QString& copy);
	public:		C operator[](size_type index) const;
	public:		QString& operator+=(const QString& s);
	public:		QString operator+(const QString& s) const;
	public:		template<size_t N> QString& operator+=(const C (&s)[N]);
	public:		template<size_t N> QString operator+(const C (&s)[N]) const;
	public:		QString& operator+=(C c);
	public:		QString operator+(C c) const;
	public:		size_type size() const;
	public:		bool empty() const;
	public:		const C* data() const;
	public:		const C* c_str() const;
	public:		QString substr(size_type offset, size_type count = npos) const;
	public:		~QString() throw();
	public:		bool operator<(const QString& r) const;
	public:		bool operator<=(const QString& r) const;
	public:		bool operator==(const QString& r) const;
	public:		bool operator!=(const QString& r) const;
	public:		bool operator>=(const QString& r) const;
	public:		bool operator>(const QString& r) const;
	public:		const_iterator begin() const { return const_iterator(pointer, this); }
	public:		const_iterator end() const { return const_iterator(pointer + length, this); }
	public:		iterator begin() { unshare(); return iterator(pointer, this); }
	public:		iterator end() { unshare(); return iterator(pointer + length, this); }
	public:		const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
	public:		const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }
	public:		reverse_iterator rbegin() { unshare(); return reverse_iterator(end()); }
	public:		reverse_iterator rend() { unshare(); return reverse_iterator(begin()); }
	public:		operator std::basic_string<C>() const { return std::basic_string<C>(pointer, length); }
	protected:	void newBuffer(size_type l, const C* b, size_type extra = 0);
	protected:	QString& append(const C* p, typename QString<C, PS>::size_type l);
	protected:	QString concat(const C* p, typename QString<C, PS>::size_type l) const;
	protected:	void unshare() const;
	protected:	void retain() const throw();
	protected:	void release() throw();
	protected:	static void copychars(C* d, const C* s, size_t l);
	protected:	C* pointer;
	protected:	size_type length;
	protected:	Buffer* buffer;
	protected:	static Buffer* obtainDummyBuffer();
};

/* --- Implementation --- */

template<> inline void QString<char>::copychars(char* d, const char* s, size_t l) { memcpy(d, s, l); }
template<> inline void QString<wchar_t>::copychars(wchar_t* d, const wchar_t* s, size_t l) { wmemcpy(d, s, l); }

template<typename C, size_t PS> class QString<C, PS>::Buffer {
	friend class QString<C, PS>;
	protected:	static Buffer*& obtainPoolHead() { static Buffer* poolHead; return poolHead; }

	public:		Buffer(size_type n) : rc(1), size(n > PS ? n : PS), chars(n > PS ? new C[n] : internal) { };
	public:		~Buffer() { if (chars != internal) delete [] chars; }

	public:		static void* operator new(size_t count) {
					assert(count == sizeof (Buffer));
					Buffer* h = obtainPoolHead();
					if (h == 0) h = reinterpret_cast<Buffer*>(::operator new(count));
					else obtainPoolHead() = obtainPoolHead()->next;
					return h;
				}

	public:		static void operator delete(void* pointer) throw() {
					Buffer* h = reinterpret_cast<Buffer*>(pointer);
					h->next = obtainPoolHead();
					obtainPoolHead() = h;
				}

	public:		static void cleanPool() throw() {
					while (obtainPoolHead() != 0) {
						Buffer* h = obtainPoolHead();
						obtainPoolHead() = h->next;
						::operator delete(h);
					}
				}

	protected:	union {
					struct {
						C internal[PS];
						int rc;
						size_type size;
						C* chars;
					};
					Buffer* next;
				};
};

template<typename C, size_t PS> void QString<C, PS>::retain() const throw() { ++buffer->rc; }
template<typename C, size_t PS> void QString<C, PS>::deinit() throw() { Buffer::cleanPool(); }
template<typename C, size_t PS> typename QString<C, PS>::size_type QString<C, PS>::size() const { return length; }
template<typename C, size_t PS> bool QString<C, PS>::empty() const { return (length == 0); }
template<typename C, size_t PS> const C* QString<C, PS>::data() const { return pointer; }

template<typename C, size_t PS> template<typename E, class Q> class QString<C, PS>::_iterator
		: public std::iterator<std::random_access_iterator_tag, E> { // FIX : is there some good base-class for this in STL?
	friend class QString;
	public:		_iterator() : p(0), s(0) { }
	public:		template<typename E2, class Q2> _iterator(const _iterator<E2, Q2>& copy) : p(copy.p), s(copy.s) { }
	protected:	explicit _iterator(E* p, Q* s) : p(p), s(s) { }
	public:		friend _iterator operator+(const _iterator& it, ptrdiff_t d) { return _iterator(it.p + d, it.s); }
	public:		friend _iterator operator+(ptrdiff_t d, const _iterator& it) { return _iterator(it.p + d, it.s); }
	public:		friend _iterator operator-(const _iterator& it, ptrdiff_t d) { return _iterator(it.p - d, it.s); }
	public:		friend ptrdiff_t operator-(const _iterator& a, const _iterator& b) {
					assert(a.s == b.s);
					return a.p - b.p;
				}
	public:		_iterator& operator+=(ptrdiff_t d) { p += d; return (*this); }
	public:		_iterator& operator-=(ptrdiff_t d) { p -= d; return (*this); }
	public:		_iterator& operator++() { ++p; return (*this); }
	public:		_iterator& operator--() { --p; return (*this); }
	public:		const _iterator operator++(int) { _iterator copy(*this); ++p; return copy; }
	public:		const _iterator operator--(int) { _iterator copy(*this); --p; return copy; }
	public:		template<typename E2, class Q2> _iterator& operator=(const _iterator<E2, Q2>& copy) {
					p = copy.p;
					s = copy.s;
					return (*this);
				}
	public:		template<typename E2, class Q2> bool operator<(const _iterator<E2, Q2>& r) const {
					assert(r.s == s);
					return (p < r.p);
				}
	public:		template<typename E2, class Q2> bool operator<=(const _iterator<E2, Q2>& r) const {
					assert(r.s == s);
					return (p <= r.p);
				}
	public:		template<typename E2, class Q2> bool operator==(const _iterator<E2, Q2>& r) const { return (p == r.p); }
	public:		template<typename E2, class Q2> bool operator!=(const _iterator<E2, Q2>& r) const {	return (p != r.p); }
	public:		template<typename E2, class Q2> bool operator>=(const _iterator<E2, Q2>& r) const {
					assert(r.s == s);
					return (p >= r.p);
				}
	public:		template<typename E2, class Q2> bool operator>(const _iterator<E2, Q2>& r) const {
					assert(r.s == s);
					return (p > r.p);
				}
	public:		E& operator*() const { assert(s->data() <= p && p < s->data() + s->size()); return *p; }
	public:		E& operator[](ptrdiff_t d) const { return *((*this) + d); }
	public:		E* p;
	public:		Q* s;
};

template<typename C, size_t PS> template<size_t N> QString<C, PS>::QString(const C (&s)[N])
		: pointer(const_cast<C*>(s)), length(N - 1), buffer(obtainDummyBuffer()) {
	retain();
}

template<typename C, size_t PS> QString<C, PS>::QString() : length(0), buffer(obtainDummyBuffer()) {
	pointer = buffer->chars;
	retain();
}
template<typename C, size_t PS> typename QString<C, PS>::Buffer* QString<C, PS>::obtainDummyBuffer() {
	static typename QString<C, PS>::Buffer dummy(0);
	return &dummy;
}

template<typename C, size_t PS> void QString<C, PS>::release() throw() {
	assert(buffer->rc >= 1);
	if (--buffer->rc == 0) delete buffer;
}

template<typename C, size_t PS> QString<C, PS>::~QString() throw() { release(); }

template<typename C, size_t PS> void QString<C, PS>::newBuffer(size_type l, const C* b, size_type extra) {
	assert(l == 0 || b != 0);
	buffer = new Buffer(l + extra + 1);
	pointer = buffer->chars;
	length = l;
	copychars(pointer, b, l);
	pointer[length] = 0;
}

template<typename C, size_t PS> QString<C, PS>::QString(C* s) {
	assert(s != 0);
	newBuffer(std::char_traits<C>::length(s), s);
}

template<typename C, size_t PS> QString<C, PS>::QString(const C* b, size_type l, size_type extra) {
	assert(b != 0);
	newBuffer(l, b, extra);
}

template<typename C, size_t PS> QString<C, PS>::QString(size_type n, C c) {
	newBuffer(0, 0, n);
	std::fill_n(pointer, n, c);
	length = n;
}

template<typename C, size_t PS> QString<C, PS>::QString(const QString& copy)
		: pointer(copy.pointer), length(copy.length), buffer(copy.buffer) {
	retain();
}

template<typename C, size_t PS> QString<C, PS>::QString(const_iterator b, const_iterator e)
		: pointer(const_cast<C*>(b.p)), length(e.p - b.p), buffer(b.s->buffer) {
	assert(b.s->begin() <= b && b <= b.s->end());
	assert(b <= e && e <= b.s->end());
	retain();
}

template<typename C, size_t PS> QString<C, PS>::QString(const C* b, const C* e) {
	assert(b != 0);
	assert(e != 0);
	assert(b <= e);
	newBuffer(e - b, b);
}

template<typename C, size_t PS> QString<C, PS>::QString(const std::basic_string<C>& copy) {
	newBuffer(copy.size(), copy.data());
}

template<typename C, size_t PS> QString<C, PS>::QString(const QString& copy, size_type offset, size_type count)
		: pointer(copy.pointer + offset), length(count < copy.length - offset ? count : copy.length - offset), buffer(copy.buffer) {
	assert((!std::numeric_limits<size_type>::is_signed || offset >= 0) && offset <= copy.length);
	retain();
}

template<typename C, size_t PS> QString<C, PS>& QString<C, PS>::operator=(const QString<C, PS>& copy) {
	copy.retain();
	this->release();
	this->pointer = copy.pointer;
	this->length = copy.length;
	this->buffer = copy.buffer;
	return (*this);
}

template<typename C, size_t PS> QString<C, PS>& QString<C, PS>::append(const C* p, size_type l) {
	assert(p != 0);
	bool fit = (pointer >= buffer->chars && pointer + length + l < buffer->chars + buffer->size);
	if (buffer->rc != 1 && fit && memcmp(pointer + length, p, l) == 0) {
		length += l;
		return (*this);
	}
	if (buffer->rc != 1 || !fit)
		(*this) = QString(pointer, length, l + (length < 65536 ? length : 65536)); // FIX : constant

	copychars(pointer + length, p, l);
	length += l;
	assert(buffer->rc == 1);
	pointer[length] = 0;
	return (*this);
}

template<typename C, size_t PS> QString<C, PS> QString<C, PS>::concat(const C* p, size_type l) const {
	assert(p != 0);
	QString copy(pointer, length, l + (l < size_type(65536) ? l : size_type(65536)));
	copychars(copy.pointer + copy.length, p, l);
	copy.length += l;
	assert(copy.buffer->rc == 1);
	copy.pointer[copy.length] = 0;
	return copy;
}

template<typename C, size_t PS> QString<C, PS>& QString<C, PS>::operator+=(const QString<C, PS>& s) {
	return (length == 0) ? (*this = s) : append(s.pointer, s.length);
}

template<typename C, size_t PS> QString<C, PS> QString<C, PS>::operator+(const QString<C, PS>& s) const {
	return (length == 0) ? s : concat(s.pointer, s.length);
}

template<typename C, size_t PS> template<size_t N> QString<C, PS>& QString<C, PS>::operator+=(const C (&s)[N]) {
	return append(s, N - 1);
}

template<typename C, size_t PS> template<size_t N> QString<C, PS> QString<C, PS>::operator+(const C (&s)[N]) const {
	return concat(s, N - 1);
}

template<typename C, size_t PS> QString<C, PS>& QString<C, PS>::operator+=(C c) { return append(&c, 1); }
template<typename C, size_t PS> QString<C, PS> QString<C, PS>::operator+(C c) const { return concat(&c, 1); }

template<typename C, size_t PS> void QString<C, PS>::unshare() const {
	if (buffer->rc != 1) *const_cast<QString<C, PS>*>(this) = QString(pointer, length);
}

template<typename C, size_t PS> const C* QString<C, PS>::c_str() const {
	if (*(pointer + length) != 0) {
		unshare();
		*(pointer + length) = 0;
	}
	return pointer;
}

template<typename C, size_t PS> QString<C, PS> QString<C, PS>::substr(size_type offset, size_type count) const {
	return QString(*this, offset, count);
}

template<typename C, size_t PS> C QString<C, PS>::operator[](size_type index) const {
	assert((!std::numeric_limits<size_type>::is_signed || 0 <= index) && index <= length);
	return (index == length) ? 0 : pointer[index];
}

template<typename C, size_t PS> bool QString<C, PS>::operator<(const QString<C, PS>& r) const {
	int d = (this->pointer == r.pointer ? 0 : std::char_traits<C>::compare
			(this->pointer, r.pointer, (this->length < r.length ? this->length : r.length)));
	return (d == 0 ? (this->length < r.length) : (d < 0));
}

template<typename C, size_t PS> bool QString<C, PS>::operator<=(const QString<C, PS>& r) const {
	int d = (this->pointer == r.pointer ? 0 : std::char_traits<C>::compare
			(this->pointer, r.pointer, (this->length < r.length ? this->length : r.length)));
	return (d == 0 ? (this->length <= r.length) : (d < 0));
}

template<typename C, size_t PS> bool QString<C, PS>::operator==(const QString<C, PS>& r) const {
	return (this->length == r.length && (this->pointer == r.pointer
			|| std::char_traits<C>::compare(this->pointer, r.pointer, this->length) == 0));
}

template<typename C, size_t PS> bool QString<C, PS>::operator!=(const QString<C, PS>& r) const { return !(*this == r); }
template<typename C, size_t PS> bool QString<C, PS>::operator>=(const QString<C, PS>& r) const { return !(*this < r); }
template<typename C, size_t PS> bool QString<C, PS>::operator>(const QString<C, PS>& r) const { return !(*this <= r); }

bool unitTest();

}

#endif
