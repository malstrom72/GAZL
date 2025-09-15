/**
	\file QStrings.cpp
	
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

#if !defined(QStrings_h)
#include "QStrings.h"
#endif
#include "assert.h"

namespace QStrings {

bool unitTest() {
	QString<char> s;
	s = "hej";
	QString<char> t = s;
	s += " du";
	s += " glade ";
	s += s.substr(4, 3);
	s += s;
	s = s + "ta en spade";
	assert(s == "hej du glade du hej du glade du ta en spade");
	assert(s < "hej du glade du hej du glade du ta en spadef");
	assert(s < "hej du glade du hej du glade du ta en spadf");
	assert(s <= "hej du glade du hej du glade du ta en spade");
	assert(s <= "hej du glade du hej du glade du ta en spadf");
	assert(s >= "hej du glade du hej du glade du ta en spadd");
	assert(s >= "hej du glade du hej du glade du ta en spade");
	assert(s > "hej du glade du hej du glade du ta en spadd");
	assert(s > "hej du glade du hej du glade du ta en spad");
	assert(s != "hej du glade du hej du glade du ta en spad");
	assert(s != "hej du glade du hej du glade du ta en spadd");
	t = "";
	const QString<char>& cs = s;
	for (QString<char>::const_iterator it = cs.begin(); it != cs.end(); ++it) {
		const char c[1] = { *it };
		t += QString<char>(c, 1);
	}
	assert(t == s);
	assert(static_cast<size_t>(cs.end() - cs.begin()) == cs.size());
	assert(cs.end() - cs.size() == cs.begin());
	assert(cs.begin() + cs.size() == cs.end());
	assert(QString<char>(cs.begin(), cs.begin() + 3) == "hej");
	assert(QString<char>(cs.begin() + 4, cs.begin() + 6) == "du");
	assert(QString<char>(cs.end() - 5, cs.end()) == "spade");

	s = "abc";
	t = s;
	const QString<char>& u = t;
	QString<char>::iterator it = t.begin();
	QString<char>::const_iterator it2 = u.begin();
	*it = 'c';
	*it = *it2;
	assert(it >= t.begin() && it <= t.end());
	assert(it2 >= u.begin() && it2 <= u.end());
	assert(s == "abc");

	return true;
}

}

#ifdef REGISTER_UNIT_TEST
REGISTER_UNIT_TEST(QStrings::unitTest)
#endif
