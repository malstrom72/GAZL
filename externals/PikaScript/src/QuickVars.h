/**
	\file QuickVars.h
	
	QuickVars is a (generally) faster version of the reference implementation's STLVariable.
	
	It achieves its better performance by caching the most recently used variables in a super-tiny hash table. The
	downside is that it uses more stack memory, especially if you have deep calling stacks with few local variables.
	
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

#ifndef QuickVars_h
#define QuickVars_h

#if !defined(PikaScriptImpl_h)
#include "PikaScriptImpl.h"
#endif

namespace Pika {

/**
	Caches a few recently used variables to speed repeated lookups.
	Example:
	QuickVars<MyVars> vars;
	Value v;
	vars.assign("a", 1);
	vars.lookup("a", v);     // caches "a"
	vars.lookup("a", v);     // hits cache
**/
template<class Super, unsigned int CACHE_SIZE = 11> class QuickVars : public Super {
	public:		typedef typename Super::ForScript::String String;
	public:		typedef typename Super::ForScript::Value Value;
	public:		typedef std::pair<String, Value> CacheEntry;

	public:		unsigned int hash(const String& s) {
					unsigned int l = static_cast<unsigned int>(s.size());
					if (s.size() == 1 && s[0] >= 'a' && s[0] <= 'z') return (s[0] - 'a') % CACHE_SIZE;
					if (s.size() == 2 && s[0] == '$' && s[1] >= '0' && s[1] <= '9') return s[1] - '0';
					return (s[0] * 1733 + s[l >> 2] * 2069 + s[l >> 1] * 2377 + s[l - 1] * 2851) % CACHE_SIZE;
				}

	public:		virtual bool assign(const String& identifier, const Value& value) {
					if (identifier.empty()) return false;
					unsigned int i = hash(identifier);
					if (cache[i].first == identifier) { cache[i] = CacheEntry(identifier, value); return true; }
					if (!cache[i].first.empty()) Super::vars[cache[i].first] = cache[i].second;
					cache[i] = CacheEntry(identifier, value);
					return true;
				}

	public:		virtual bool erase(const String& identifier) {
					bool erased = (Super::vars.erase(identifier) != 0);
					unsigned int i = hash(identifier);
					if (cache[i].first == identifier) { cache[i] = std::pair<const String, Value>(); erased = true; }
					return erased;
				}

	public:		virtual bool lookup(const String& identifier, Value& result) {
					if (identifier.empty()) return false;
					unsigned int i = hash(identifier);
					if (cache[i].first == identifier) { result = cache[i].second; return true; }
					typename Super::VariableMap::const_iterator it = Super::vars.find(identifier);
					if (it == Super::vars.end()) return false;
					if (!cache[i].first.empty()) Super::vars[cache[i].first] = cache[i].second;
					cache[i] = CacheEntry(identifier, it->second);
					result = it->second;
					return true;
				}

	public:		virtual void list(const String& key, typename Super::ForScript::Variables::VarList& list) {
					for (unsigned int i = 0; i < CACHE_SIZE; ++i)
						if (!cache[i].first.empty()) Super::vars[cache[i].first] = cache[i].second;
					for (typename Super::VariableMap::const_iterator it = Super::vars.lower_bound(key)
							; it != Super::vars.end() && it->first.substr(0, key.size()) == key; ++it)
						list.push_back(*it);
				}

	protected:	CacheEntry cache[CACHE_SIZE];
};

} // namespace Pika

#endif
