/*
	The correct answer to the search is element 14528 (containing 143350)
	
	Unoptimized C++ (gcc -O0), global locals (i.e. memory[n] used for locals)

		unsigned int myrand() {
			static unsigned int seed = 2463534242U;
			unsigned int y = seed;
			y ^= y << 13;
			y ^= y >> 17;
			seed = y ^ (y << 5);
			return seed;
		}
		
		float array[20000];
		
		int main(int, char**) {
			for (int i = 0; i < 20000; ++i) {
				array[i] = myrand();
			}

			for (int i = 0; i < 2000; ++i) {
				int minj = 0;
				for (int j = 1; j < 20000; ++j) {
					if (array[j] < array[minj]) minj = j;
				}
				volatile static int result = minj;
			}
		
			return 0;
		}

		= 0.202 secs (we only time the last loop, not the random creation stuff)
		
	Optimized C++ (gcc -O3), locals
		
		= 0.173 secs

	Optimized C++ (icc -O3), locals
	
		= 0.101 secs (0.15 secs if all variables are volatile globals, i.e. not in registers)
 
	llvm lli (jit)
 
		= 0.17 secs

	llvm lli (interpreting)
 
		= 130 secs ca

	AudioClay
		
		RandomInt2.acl
			seed : 2463534242;
			output y = seed;
			y ^= y << 13;
			y ^= y >>> 17;
			y ^= y << 5;
			seed = y;
		
		RandomIntTest.acl
			using utils;
			RandomInt2 rnd;
			reset rnd;

			CPUStats stats();
			static Float array[20000];
			for (i = 0; i < 20000) {
				x = rnd();
				y = (x < 0 ? 4294967296.0 + x : x);
				array[i] = y;
			};
			stats();
			minj = 0;
			for (k = 0; k < 2000) { minj = 0; for (j = 1; j < 20000) if (array[j] < array[minj]) minj = j; }
			stats();

			Sleep(0.5);
		
		= 0.14 secs

	NuXScript:
	
			seed = 2463534242;
			myrand = function() { var y = seed; y ^= y << 13; y ^= y >> 17; seed = y ^ (y << 5); return seed; };
			memory = new Array();
			for (i = 0; i < 20000; ++i) memory[i] = myrand() & 0xFFFFFFFF;
			var f;
			for (i = 0; i < 2000; ++i) { f = 0; for (j = 1; j < 20000; ++j) if (memory[j] < memory[f]) f = j; };
		
		= 48.55 secs
		
	Safari Javascript:
	
			clock = Date.getTime();
			seed = 2463534242;
			myrand = function() { var y = seed; y ^= y << 13; y ^= y >>> 17; seed = y ^ (y << 5); return seed; };
			memory = new Array();
			for (i = 0; i < 20000; ++i) memory[i] = myrand() >>> 0;
			var f;
			startClock = clock();
			for (i = 0; i < 2000; ++i) { f = 0; for (j = 1; j < 20000; ++j) if (memory[j] < memory[f]) f = j }
			out(clock() - startClock);

		= 26.231 secs
		
	Rhino (Java JS)
	
		= 81 secs
		
	Internet Explorer
	
		around 70 secs
		
	V8
	
		1.6 secs (not bad!)
		
	TraceMonkey (SpiderMonkey)
	
	Without NanoJit: 8 secs
	With NanoJIT: 1.2 secs
		
	LUA (requires LuaBit library)
	
			require('bit')
			seed = 2463534242
			function myrand()
				local y = seed
				y = bit.bxor(y, bit.blshift(y, 13))
				y = bit.bxor(y, bit.blogic_rshift(y, 17))
				y = bit.bxor(y, bit.blshift(y, 5))
				seed = y
				return y
			end
			function test()
				array = {}
				for i = 0, 19999 do array[i] = myrand() end
				local startClock = os.clock()
				for i = 1, 2000 do
					f = 0
					for j = 1, 19999 do
						if array[j] < array[f] then f = j end
					end
				end
				print(os.clock() - startClock)
				print(f)
			end
			
		= 12 secs (pretty good!)
		
	PikaCmd:
	
			myrand.seed = 2463534242;
			myrand = function {
				y = myrand.seed;
				y ^= y << 13;
				y ^= y >> 17;
				( (::myrand.seed = y ^ (y << 5)) )
			};
			for (i = 0; i < 20000; ++i) memory[i] = myrand();

			clock(>for (i = 0; i < 2000; ++i) { f = 0; for (j = 1; j < 20000; ++j) if (memory[j] < memory[f]) f = j; })
			
		= 155.33576099845 secs
			
			prune(@memory);
			myrand.seed = 2463534242;
			for (i = 0; i < 20000; ++i) mem[i] = myrand();
			clock(>for(i=0;++i<=2000;)for({f=0;j=0};++j<20000;)if(mem[j]<mem[f])f=j)

		= 140.93802330703 secs (funny enough if we used 'm' instead of 'mem' we got much worse performance)
	
	SillyVM variant 1 ("string style" code, unioned Float / Int statcks)
	
		= 7.68 secs
		
	SillyVM variant 5 (32-bit instructions, separate Float / Int stacks, no bounds checkings whatsoever)
	
		= 3.4 secs
	
	SillyVM variant 7011 (instruction struct, operands are direct memory pointers, even constants, jumps are direct pointers)
	
		= 0.54 secs (ICC), 0.71 secs (GCC)

	SillyVM variant 7012 (operands are local stack-pointer relative indices or immediates, jumps are relative)
	
		= 0.63 secs (ICC), 0.68 secs (GCC)
*/

#include "assert.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <exception>
#include <string>
#include <algorithm>
#include <iostream>
#include <map>

class PMachine;
typedef int Int;
typedef unsigned int UInt;
typedef float Float;
typedef UInt Offset;
typedef UInt Size;
typedef Int (*CFunc)(PMachine*);

union Value {
	Int i;
	Float f;
};
Value toValue(Int value) { Value v; v.i = value; return v; }
Value toValue(UInt value) { Value v; v.i = UInt(value); return v; }
Value toValue(Float value) { Value v; v.f = value; return v; }
bool operator<(const Value& a, const Value& b) { return a.i < b.i; };
Value dummyValue = { 0 };

class PMachine;

enum POpcode {
	pMOVE = 0x477a, pMOVEc, pGET, pSET, pSETc, pPEEK, pPEEKc, pPOKE, pPOKEvc, pPOKEcc
	, pADD, pADDc, pSUB, pSUBvc, pSUBcv, pMUL, pMULc, pDIV, pDIVvc, pDIVcv
	, pMOD, pMODvc, pMODcv, pAND, pANDc, pOR, pORc, pXOR, pXORc, pSHL, pSHLvc, pSHLcv, pSHR, pSHRvc, pSHRcv
	, pADDf, pADDfc, pSUBf, pSUBfvc, pSUBfcv, pMULf, pMULfc, pDIVf, pDIVfvc, pDIVfcv, p2INT, p2FLT, pLEAD
	, pSWCH, pCFUN, pCALL, pCALLc, pFUNC, pRET
	, pFORU, pFORD, pFORUc, pFORDc, pLT, pLTvc, pLTcv, pEQ, pEQc, pNLT, pNLTvc, pNLTcv
	, pNEQ, pNEQc, pLTf, pLTfvc, pLTfcv, pNLTf, pNLTfvc, pNLTfcv, pEQf, pEQfc, pNEQf, pNEQfc, pGOTO
	, pLABEL, pCASE, pEND
};

struct Instruction {
	POpcode opcode;
	union { Int o0; Value c0; const Instruction* jump; const Instruction** jumpTable; Int (*func)(PMachine*, Value&, Value&); const char* label; }; // FIX : probably remove 'label' when I do some other asm solution
	union { Int o1; Value c1; };
	union { Int o2; Value c2; };
};

class PMachine {
	public:		PMachine(Size codeSize, const Instruction* code, Size memorySize, Value* memory, Size memoryRWSize, Size vStackSize, Size ipStackSize
						, const Instruction** ipStack, const CFunc* cFuncTable)
					: codeSize(codeSize)
					, codeBase(code)
					, memorySize(memorySize)
					, memoryBase(memory)
					, memoryRWSize(memoryRWSize)
					, vStack(memory + memoryRWSize - vStackSize)
					, vStackEnd(memory + memoryRWSize)
					, ipStack(ipStack)
					, ipStackEnd(ipStack + ipStackSize)
					, cFuncTable(cFuncTable)
					, ip(code)
					, vsp(vStack)
					, ipsp(ipStack)
				{
					assert(vStackSize < memoryRWSize);
				}
	public:		enum { OK = 0, OPCOUNT_LIMIT = -1, BAD_READ = -2, BAD_WRITE = -3, CALL_STACK_OVERFLOW = -4, BAD_CALL = -5, STACK_OVERFLOW = -6 };
	protected:	static UInt switchHop(const Value* jumpTable, UInt index) {
					return (index >= jumpTable[-1].i ? 0 : jumpTable[index].i);
				}
	public:		Value* memory() { return memoryBase; }
	public:		Value& arg(UInt index) { return (vsp + index >= vStackEnd) ? dummyValue : vsp[index]; }
	public:		Int execute(Int opcount);
public:		void jump(const Instruction* instructionPointer) { ip = instructionPointer; }
public:		Value* stackPointer() const { return vsp; }
public:		Value* stackBase() const { return vStack; }
public:		void push(const Value& v) { assert(vsp > vStack); *--vsp = v; }
	
	protected:	Size const codeSize;
	protected:	const Instruction* const codeBase;
	protected:	Size const memorySize;
	protected:	Value* const memoryBase;
	protected:	Size const memoryRWSize;
	protected:	Value* const vStack;
	protected:	Value* const vStackEnd;
	protected:	const Instruction** const ipStack;
	protected:	const Instruction** const ipStackEnd;
	protected:	const CFunc* const cFuncTable;
	protected:	const Instruction* ip;
	protected:	Value* vsp;
	protected:	const Instruction** ipsp;
};

Int PMachine::execute(Int opcount) {
	const Instruction* $ip = ip;
	Value* $vsp = vsp;
	const Instruction** $ipsp = ipsp;
	UInt index;
	Int err;
	Value* $mb = memoryBase;
	const Instruction* nip;

	#define P0 ($vsp[$ip->o0])
	#define P1 ($vsp[$ip->o1])
	#define P2 ($vsp[$ip->o2])
	#define P0i (P0.i)
	#define P1i (P1.i)
	#define P2i (P2.i)
	#define P0f (P0.f)
	#define P1f (P1.f)
	#define P2f (P2.f)
	#define P0c ($ip->c0)
	#define P1c ($ip->c1)
	#define P2c ($ip->c2)
	#define P0ci (P0c.i)
	#define P1ci (P1c.i)
	#define P2ci (P2c.i)
	#define P0cf (P0c.f)
	#define P1cf (P1c.f)
	#define P2cf (P2c.f)
	#define JUMP { ($ip += P0ci); continue; }
	
	err = OPCOUNT_LIMIT;
	while (--opcount >= 0) {
		switch ($ip->opcode) {
			case pMOVE:		P0 = P1; break;
			case pMOVEc:	P0 = P1c; break;
			case pGET:		P0 = $mb[$ip->o1]; break;
			case pSET:		$mb[$ip->o0] = P1; break;
			case pSETc:		$mb[$ip->o0] = P1c; break;
			case pPEEK:		index = P1i + P2i; goto peek;
			case pPEEKc:	index = P1i + P2ci;
			peek:			if (index >= memorySize) { err = BAD_READ; goto done; }
							P0 = $mb[index];
							break;
			case pPOKE:		index = P0i + P1i;
							if (index >= memoryRWSize) { err = BAD_WRITE; goto done; }
							$mb[index] = P2;
							break;
			case pPOKEvc:	index = P0i + P1i; goto pokec;
			case pPOKEcc:	index = P0i + P1ci;
			pokec:			if (index >= memoryRWSize) { err = BAD_WRITE; goto done; }
							$mb[index] = P2c;
							break;
			case pADD:		P0i = P1i + P2i; break;
			case pADDc:		P0i = P1i + P2ci; break;
			case pSUB:		P0i = P1i - P2i; break;
			case pSUBvc:	P0i = P1i - P2ci; break;
			case pSUBcv:	P0i = P1ci - P2i; break;
			case pMUL:		P0i = P1i * P2i; break;
			case pMULc:		P0i = P1i * P2ci; break;
			case pDIV:		P0i = P1i / P2i; break;
			case pDIVvc:	P0i = P1i / P2ci; break;
			case pDIVcv:	P0i = P1ci / P2i; break;
			case pMOD:		P0i = P1i % P2i; break;
			case pMODvc:	P0i = P1i % P2ci; break;
			case pMODcv:	P0i = P1ci % P2i; break;
			case pAND:		P0i = P1i & P2i; break;
			case pANDc:		P0i = P1i & P2ci; break;
			case pOR:		P0i = P1i | P2i; break;
			case pORc:		P0i = P1i | P2ci; break;
			case pXOR:		P0i = P1i ^ P2i; break;
			case pXORc:		P0i = P1i ^ P2ci; break;
			case pSHL:		P0i = P1i << P2i; break;
			case pSHLvc:	P0i = P1i << P2ci; break;
			case pSHLcv:	P0i = P1ci << P2i; break;
			case pSHR:		P0i = UInt(P1i) >> P2i; break;
			case pSHRvc:	P0i = UInt(P1i) >> P2ci; break;
			case pSHRcv:	P0i = UInt(P1ci) >> P2i; break;
			case pADDf:		P0f = P1f + P2f; break;
			case pADDfc:	P0f = P1f + P2cf; break;
			case pSUBf:		P0f = P1f - P2f; break;
			case pSUBfvc:	P0f = P1f - P2cf; break;
			case pSUBfcv:	P0f = P1cf - P2f; break;
			case pMULf:		P0f = P1f * P2f; break;
			case pMULfc:	P0f = P1f * P2cf; break;
			case pDIVf:		P0f = P1f / P2f; break;
			case pDIVfvc:	P0f = P1f / P2cf; break;
			case pDIVfcv:	P0f = P1cf / P2f; break;
			case p2INT:		P0i = static_cast<Int>(P1f * P2cf); break;
			case p2FLT:		P0f = static_cast<Float>(P1i) * P2cf; break;
			case pLEAD:		P0i = &P1 - $mb + P2ci; break;
			case pSWCH:		if (UInt(P1i) < P2ci) { $ip += $mb[P0ci + P1i].i; continue; } break;
			case pCFUN:		if ((err = (*cFuncTable[UInt(P0ci)])(this)) != 0) goto done; break;
			case pCALLc:	nip = $ip + P0ci; goto call;
			case pCALL:		if (UInt(P0i) >= codeSize || (nip = codeBase + P0i)->opcode != pFUNC) { err = BAD_CALL; goto done; }
			call:			*$ipsp++ = $ip;
							if ($ipsp >= ipStackEnd) { err = CALL_STACK_OVERFLOW; goto done; }
							$ip = nip;
							assert($ip->opcode == pFUNC);
							continue;
			case pFUNC:		if (($vsp += UInt(P0ci)) + UInt(P1ci) > vStackEnd) { err = STACK_OVERFLOW; goto done; }
							break;
			case pRET:		$vsp -= UInt(P0ci);
							assert($vsp >= vStack);
							if ($ipsp == ipStack) { err = OK; goto done; }
							$ip = *--$ipsp;
							break;
			case pFORU:		if (++P1i < P2i) JUMP; break;
			case pFORUc:	if (++P1i < P2ci) JUMP; break;
			case pFORD:		if (--P1i >= P2i) JUMP; break;
			case pFORDc:	if (--P1i >= P2ci) JUMP; break;
			case pLT:		if (P1i < P2i) JUMP; break;
			case pLTvc:		if (P1i < P2ci) JUMP; break;
			case pLTcv:		if (P1ci < P2i) JUMP; break;
			case pNLT:		if (P1i >= P2i) JUMP; break;
			case pNLTvc:	if (P1i >= P2ci) JUMP; break;
			case pNLTcv:	if (P1ci >= P2i) JUMP; break;
			case pEQ:		if (P1i == P2i) JUMP; break;
			case pEQc:		if (P1i == P2ci) JUMP; break;
			case pNEQ:		if (P1i != P2i) JUMP; break;
			case pNEQc:		if (P1i != P2ci) JUMP; break;
			case pLTf:		if (P1f < P2f) JUMP; break;
			case pLTfvc:	if (P1f < P2cf) JUMP; break;
			case pLTfcv:	if (P1cf < P2f) JUMP; break;
			case pNLTf:		if (P1f >= P2f) JUMP; break;
			case pNLTfvc:	if (P1f >= P2cf) JUMP; break;
			case pNLTfcv:	if (P1cf >= P2f) JUMP; break;
			case pEQf:		if (P1f == P2f) JUMP; break;
			case pEQfc:		if (P1f == P2cf) JUMP; break;
			case pNEQf:		if (P1f != P2f) JUMP; break;
			case pNEQfc:	if (P1f != P2cf) JUMP; break;
			case pGOTO:		JUMP;
		}
		++$ip;
	}
done:
	ip = $ip;
	vsp = $vsp;
	ipsp = $ipsp;
	return err;
}


UInt myrand() {
	static UInt seed = 2463534242U;
	UInt y = seed;
	y ^= y << 13;
	y ^= y >> 17;
	seed = y ^ (y << 5);
	return seed;
}

Int sin(PMachine* m) { m->arg(0).f = sinf(m->arg(1).f); return 0; }
Int mydefault(PMachine* m) { std::cout << "default" << std::endl; return 0; }
Int caseTwo(PMachine* m) { std::cout << "case2" << std::endl; return 0; }
Int caseTwentyThree(PMachine* m) { std::cout << "case23" << std::endl; return 0; }
Int printInt(PMachine* m) { std::cout << m->arg(0).i << std::endl; return 0; }

const CFunc cFuncs[] = { sin, mydefault, caseTwo, caseTwentyThree, printInt };

	volatile Float array[20002];
	volatile Int result;
	volatile Int i;
	volatile Int j;
	volatile Int minj;
	volatile Int fibbI = 14;

class Assembler {
	protected:	template<typename T> class FixArrayRef {
					public:		FixArrayRef(size_t maxCount, T* elements) : maxCount(maxCount), count(0), elements(elements) { }
					public:		operator const T*() const { return elements; }
					public:		operator T*() { return elements; }
					public:		T* data() const { return elements; }
					public:		const T& operator[](UInt index) const { assert(index < count); return elements[index]; }
					public:		T& operator[](UInt index) { assert(index < count); return elements[index]; }
					public:		T& push_back(const T& element) { if (count >= maxCount) throw std::exception(); return (elements[count++] = element); }
					public:		size_t capacity() const { return maxCount; }
					public:		size_t size() const { return count; }
					public:		void resize(size_t newSize) { if (newSize > maxCount) throw std::exception(); count = newSize; }
					public:		bool empty() const { return count == 0; }
					protected:	size_t maxCount;
					protected:	size_t count;
					protected:	T* elements;
				};

	public:		Assembler(Int maxLocals, Value* locals, Int maxGlobals, Value* globals
						, Int maxJumpTableEntries, const Instruction** jumpTableEntries)
						: globals(maxGlobals, globals)
						, locals(maxLocals, locals)
						, jumpTables(maxJumpTableEntries, jumpTableEntries) {
					for (Int i = 0; i < 10; ++i) global(std::string("%") + static_cast<char>('0' + i));
				}
/*	public:		Value* constant(const Value& v) {
					std::map<Value, Value*>::const_iterator it = constantIndices.find(v);
					return (it != constantIndices.end()) ? it->second : (constantIndices[v] = &constants.push_back(v));
				}*/
	public:		Offset global(const std::string& s) {
					std::map<std::string, Offset>::const_iterator it = globalIndices.find(s);
					return (it != globalIndices.end()) ? it->second : (globalIndices[s] = (&globals.push_back(toValue(0)) - globals.data()));
				}
	public:		Offset local(const std::string& s) {
					std::map<std::string, Offset>::const_iterator it = localIndices.find(s);
					return (it != localIndices.end()) ? it->second : (localIndices[s] = (&locals.push_back(toValue(0 /*0xFFCCEEBB*/)) - locals.data()));
				}
	protected:	Offset allocateArray(const std::string& s, Size count, std::map<std::string, Offset>& indices, FixArrayRef<Value>& memory) {
					std::map<std::string, Offset>::const_iterator it = indices.find(s);
					if (it != indices.end()) {
//						if (count != it->second[-1].i) throw std::exception();
						return it->second;
					} else {
						Value* a = (&memory.push_back(toValue(count)) + 1);
						memory.resize(memory.size() + count);
						std::fill_n(a, count, toValue(0));
						indices[s] = (a - memory);
						return indices[s];
					}
				}
	public:		Offset localArray(const std::string& s, Size count) { return allocateArray(s, count, localIndices, globals); }
	public:		Offset globalArray(const std::string& s, Size count) { return allocateArray(s, count, globalIndices, globals); }
	public:		const Instruction* localLabel(const std::string& s) {
					std::map<std::string, Instruction*>::const_iterator it = localLabels.find(s);
					if (it == localLabels.end()) throw std::exception();
					return it->second;
				}
	public:		const Instruction* globalLabel(const std::string& s) {
					std::map<std::string, Instruction*>::const_iterator it = globalLabels.find(s);
					if (it == globalLabels.end()) throw std::exception();
					return it->second;
				}
	public:		const Instruction* label(const char* s) { return (s[0] == '@' ? localLabel(s) : globalLabel(s)); };
	public:		Offset operator()(const char* s) { return (s[0] == '$' ? local(s) : global(s)); }
	public:		Offset operator()(const char* s, Size count) { return (s[0] == '$' ? localArray(s, count) : globalArray(s, count)); }
	public:		Int im(Int i) { return *reinterpret_cast<Int*>(&i); }
	public:		Int im(UInt i) { return *reinterpret_cast<Int*>(&i); }
	public:		Int im(Float f) { return *reinterpret_cast<Int*>(&f); }
	public:		void newLocals() { localIndices.clear(); localLabels.clear(); }
	public:		Int resolveJumps(Instruction* code) {
					const Instruction* in = code;
					Instruction* out = code;

					std::map<std::string, const Instruction*> switches;
					POpcode op;
					do {
						op = in->opcode;
						switch (op) {
							case pLABEL: {
								std::string s(in->label);
								if (in->label[0] == '@') {
									if (localLabels.find(s) != localLabels.end()) throw std::exception();
									localLabels[s] = out;
								} else {
									if (globalLabels.find(s) != globalLabels.end()) throw std::exception();
									globalLabels[s] = out;
								}
								++in;
								break;
							}

#if 0							
							case pSWCH: {
								*out = *in++;								
								std::string s(out->label);
								if (switches.find(s) != switches.end()) throw std::exception();
								Size count = out->v1->i;
								size_t oldSize = jumpTables.size();
								jumpTables.resize(oldSize + count);
								const Instruction** jumpTable = &jumpTables[oldSize];
								out->jumpTable = jumpTable;
								out->jumpTableSize = count;
								switches[s] = out;
								++out;
								std::fill_n(jumpTable, count, (const Instruction*)(0));
								break;
							}

							case pCASE: {
								std::string s(in->label);
								std::map<std::string, const Instruction*>::const_iterator it = switches.find(s);
								if (it == switches.end()) throw std::exception();
								Int index = in->v1->i;
								if (UInt(index) >= it->second->jumpTableSize) throw std::exception();
								if (it->second->jumpTable[index] != 0) throw std::exception();
								it->second->jumpTable[index] = out;
								++in;
								break;
							}
#endif
							
							default: *out++ = *in++; break;
						}
					} while (op != pEND);

					for (Instruction* out2 = code; out2 != out; ++out2) {
						switch (out2->opcode) {
							case pGOTO: case pFORU: case pFORD: case pFORUc: case pFORDc: case pLT: case pLTvc: case pLTcv:
							case pEQ: case pEQc: case pNLT: case pNLTvc: case pNLTcv: case pNEQ: case pNEQc:
							case pLTf: case pLTfvc: case pLTfcv: case pNLTf: case pNLTfvc: case pNLTfcv:
							case pEQf: case pEQfc: case pNEQf: case pNEQfc: case pCALLc:
								out2->c0.i = label(out2->label) - out2;
								break;
#if 0								
							case pSWCH: {
								for (Int i = 0; i < out2->jumpTableSize; ++i) {
									if (out2->jumpTable[i] == 0) out2->jumpTable[i] = out2 + 1;
								}
							}
							break;
#endif
						}
					}
					
					return out - code;
				}

	protected:	FixArrayRef<Value> globals;
	protected:	FixArrayRef<Value> locals;
	protected:	FixArrayRef<const Instruction*> jumpTables;
	protected:	std::map<std::string, Instruction*> localLabels;
	protected:	std::map<std::string, Instruction*> globalLabels;
	protected:	std::map<std::string, Offset> globalIndices;
	protected:	std::map<std::string, Offset> localIndices;
};

static Int fibbc(Int k) { return (k <= 2) ? 1 : (fibbc(k - 1) + fibbc(k - 2)); };

Int main(Int argc, const char** argv) {
	#if defined(NDEBUG)
		const Int iterations = 2000;
	#else
		const Int iterations = 1;
	#endif

	const Instruction* ipStack[32768];
	
	Value regs[256];
	Value globals[100000 + 10000];
	std::fill_n(&globals[100000], 10000, toValue(0xAACC5599));
	const Instruction* jumpTableEntries[30000];
	
	Assembler _(256, regs, 100000, globals, 30000, jumpTableEntries);




	

	const Instruction ptest1[] = {
		  { pFUNC, _.im(10), _.im(0) }
		, { pSETc, _("%R"), _.im(0) }
		, { pMOVEc, _("$idx"),	_.im(1)					}
		, { pMOVEc, _("$mini"),	_.im(0)					}
		, { pGET, _("$minv"),	_("array", 20000)		}
		, { pPEEKc, _("$elem"), _("$idx"), _("array", 20000) }
		, { pNLTf, 3, _("$elem"), _("$minv")			}
		, { pMOVE, _("$minv"),	_("$elem")				}			
		, { pMOVE, _("$mini"),	_("$idx")				}			
		, { pFORUc, -4, _("$idx"),	_.im(20000)			}
		, { pSET, _("%R"), _("$mini")					}
		, { pRET, _.im(10) }
		, { pEND }
	};

/*
	MOVE $idx 1						
	MOVE $mini 0						
	GETE $minv array $mini		
	@loop 
		GETE $val array $idx	
		NLTf @notLess $val $minv; MOVE $minv $val; MOVE $mini $idx; @notLess 
	FORU @loop $idx 20000					
	EXIT 

*/
	_.newLocals();
	Instruction ptest2[] = {
		  { pLABEL, Offset("ptest2") }
		, { pFUNC, _.im(10), _.im(0) }
		, { pGET, _("$array"), _("%0") }
		, { pMOVEc, _("$idx"),	_.im(1)						}
		, { pMOVEc, _("$mini"),	_.im(0)						}
		, { pPEEK, _("$minv"),	_("$array"),	_("$mini")		}
		, { pLABEL, Offset("@loop") }
			, { pPEEK, _("$val"),	_("$array"),	_("$idx")	}
			, { pNLTf, Offset("@notLess"),	_("$val"),	_("$minv")	}
				, { pMOVE, _("$minv"),	_("$val")			}			
				, { pMOVE, _("$mini"),	_("$idx")			}			
			, { pLABEL, Offset("@notLess") }
		, { pFORUc, Offset("@loop"), _("$idx"), _.im(20000)			}	
		, { pSET, _("%R"),	_("$mini")						}
		, { pRET, _.im(10) }
		, { pEND }
	};
	_.resolveJumps(ptest2);
	
	_.newLocals();
	Instruction ptest3[] = {
		  { pLABEL, Offset("ptest3") }
		, { pMOVEc, _("$idx"),	_.im(0)						}
		, { pLABEL, Offset("@loop") }
			, { pSHLvc, _("$t"), _("$seed"), _.im(13) }
			, { pXOR, _("$seed"), _("$seed"), _("$t") }
			, { pSHRvc, _("$t"), _("$seed"), _.im(17) }
			, { pXOR, _("$seed"), _("$seed"), _("$t") }
			, { pSHLvc, _("$t"), _("$seed"), _.im(5) }
			, { pXOR, _("$seed"), _("$seed"), _("$t") }
		, { pFORUc, Offset("@loop"), _("$idx"), _.im(20000)				}	
		, { pRET }
		, { pEND }
	};
	_.resolveJumps(ptest3);

	_.newLocals();
	Instruction randy[] = {
		{ pLABEL, Offset("myrand") }
		, { pSHLvc, _("$t"), _("seed"), _.im(13) }
		, { pXOR, _("seed"), _("seed"), _("$t") }
		, { pSHRvc, _("$t"), _("seed"), _.im(17) }
		, { pXOR, _("seed"), _("seed"), _("$t") }
		, { pSHLvc, _("$t"), _("seed"), _.im(5) }
		, { pXOR, _("seed"), _("seed"), _("$t") }
		, { pMOVE, _("%R"), _("seed") }
		, { pRET }
		, { pEND }
	};
	_.resolveJumps(randy);

	enum { A0 = -2, A1 = -1, C0 = 0, C1 = 1 };
	_.newLocals();
	Instruction fibb[] = {
		  { pLABEL, Offset("fib") }
		, { pFUNC, _.im(2), _.im(2) }
		, { pNLTcv, Offset("@noRecurse"), _.im(2), A1 }
			, { pSUBvc, C1, A1, _.im(1) }
			, { pCALLc, Offset("fib") }
			, { pMOVE, A0, C0 }
			, { pSUBvc, C1, A1, _.im(2) }
			, { pCALLc, Offset("fib") }
			, { pADD, A0, A0, C0 }
			, { pRET, _.im(2) }
		, { pLABEL, Offset("@noRecurse") }
			, { pMOVEc, A0, _.im(1) }
			, { pRET, _.im(2) }
			, { pEND }
	};
	_.resolveJumps(fibb);
	
	_.newLocals();
	Instruction fibbTest[] = {
		  { pLABEL, Offset("fibtest") }
		, { pMOVEc, _("$i"), _.im(0) }
		, { pLABEL, Offset("@loop") }
			, { pCALLc, Offset("fib"), _.im(6), _.im(6) }
		, { pFORUc, Offset("@loop"), _("$i"), _.im(iterations) }
		, { pRET }
		, { pEND }
	};
	_.resolveJumps(fibbTest);

	_.newLocals();
	Instruction ptest4[] = {
		  { pLABEL, Offset("ptest4") }
//		, { pRESVc, _("$a"), _.im(20000) }
		, { pMOVEc, _("seed"), _.im(0x92d68ca2) }
		, { pMOVEc, _("$i"), _.im(0) }
		, { pLABEL, Offset("@loop1") }
			, { pCALLc, Offset("myrand"), _("%0"), _("%0") }
			, { p2FLT, _("$v"), _("%R"), _.im(1.0f) }
			, { pNLTfvc, Offset("@pos"), _("$v"), _.im(0.0f) }
				, { pADDfc, _("$v"), _("$v"), _.im(4294967296.0f) }
			, { pLABEL, Offset("@pos") }
			, { pPOKE, _("$a"), _("$i"), _("$v") }
		, { pFORUc, Offset("@loop1"), _("$i"), _.im(20000) }
		, { pMOVEc, _("$i"), _.im(0) }
		, { pLABEL, Offset("@loop2") }
			, { pCALLc, Offset("ptest2"), _("$a"), _("%0") }
		, { pFORUc, Offset("@loop2"), _("$i"), _.im(iterations) }
//		, { pFREE, _("$a") }
		, { pRET }
		, { pEND }
	};
	_.resolveJumps(ptest4);

	_.newLocals();
	Instruction ptest5[] = {
		  { pLABEL, Offset("ptest5") }
		, { pMOVEc, _("$j"), _.im(0) }
		, { pLABEL, Offset("@loop2") }
			, { pMOVEc, _("$i"), _.im(0) }
			, { pLABEL, Offset("@loop1") }
				, { p2FLT, _("$x"), _("$i"), _.im(0.000015915494309189f) }
				, { pCFUN, _.im(0), _("$y"), _("$x") }
				, { pPOKE, _("array", 20000), _("$i"), _("$y") }
			, { pFORUc, Offset("@loop1"), _("$i"), _.im(20000) }
		, { pFORUc, Offset("@loop2"), _("$j"), _.im(iterations) }
		, { pRET }
		, { pEND }
	};
	_.resolveJumps(ptest5);

	_.newLocals();
	Instruction ptest6[] = {
		  { pLABEL, Offset("@func") }
			, { pMOVE, _("$i"), _("%0") }
			, { pSWCH, Offset("@sw1"), 25, _("$i") }
					, { pCFUN, _.im(1), _("%0"), _("%0") }
					, { pGOTO, Offset("@endSwitch") }
				, { pCASE, Offset("@sw1"), 2 }
					, { pCFUN, _.im(2), _("%0"), _("%0") }
					, { pGOTO, Offset("@endSwitch") }
				, { pCASE, Offset("@sw1"), 23 }
					, { pCFUN, _.im(3), _("%0"), _("%0") }
			, { pLABEL, Offset("@endSwitch") }
		, { pRET }
		, { pLABEL, Offset("ptest6") }
			, { pMOVEc, _("$j"), _.im(30) }
			, { pLABEL, Offset("@loop") }
				, { pCFUN, _.im(4), _("$j"), _("%0") }
				, { pCALLc, Offset("@func"), _("$j"), _("%0") }
			, { pFORDc, Offset("@loop"), _("$j"), _.im(-3) }
		, { pRET }
		, { pEND }
	};
	_.resolveJumps(ptest6);
	
/*	ptest3[2].test.i = 13;
	ptest3[4].test.i = 17;
	ptest3[6].test.i = 5;*/
	
	Offset o = _("array", 20000);
	Value* a = &globals[o];
	for (Int i = 0; i < 20000; ++i) a[i].f = myrand();

/*	{
		for (Int i = 0; i < 20000; ++i) array[i] = myrand();
		array[6653] = -0.1f;
	}*/
	{	
		clock_t c0 = clock();
		Int status = 0;
//		RegMachine5 machine(regs, memory);
#if 1
		for (Int i = 0; i < iterations; ++i) 
//	for (Int j = 0; j < 20000; ++j)	myrand();
	//	execute1(0x7FFFFFFF, strlen(test) + 1, test, 65536, memory);
	//	execute2(0x7FFFFFFF, strlen(test) + 1, test, 65536, memory, 32768, stack);
	//	execute3(0x7FFFFFFF, strlen(test) + 1, test, 65536, memory, 32768, intStack, floatStack);
	//	execute4(0x7FFFFFFF, strlen(test) + 1, test, 65536, memory, 32768, intStack, floatStack);
	//	execute6(0x7FFFFFFF, sizeof (test3) / sizeof (*test3), test3, 65536, memory, 16, intStack, floatStack);
	//	execute7(0x7FFFFFFF, sizeof (test3) / sizeof (*test3), test3, 65536, memory, 16, stack);
	//	regMachine1(0x7FFFFFFF, rTest3, memory);
	//	machine2.execute(rTest3);
	//	machine3.execute(rm3test);
	//	machine.execute(rm5test);
	//	machine.execute(rm6test);
	//	result = memory[1].i;

		{
			PMachine pmachine(sizeof (ptest1) / sizeof (*ptest1), ptest1, 100000, globals, 80000, 50000, 256, ipStack, cFuncs);
			status = pmachine.execute(0x7FFFFFFF);
		}
		Offset o = _("%R");
		Int result = globals[o].i;
/*	
		{
			for (j = 0; j < 20000; ++j) result = fibbc(fibbI);
		}
*/	
/*		{
			minj = 1;
			for (j = 1; j < 20000; ++j) if (array[j] < array[minj]) minj = j;
			result = minj;
		}*/
#elif 1
		const Instruction* entry = fibb;
			PMachine pmachine(sizeof (fibb) / sizeof (*fibb), fibb, 100000, globals, 80000, 50000, 256, ipStack);
for (j = 0; j < 200000; ++j) 
		{
			pmachine.stackPointer()[1] = toValue(14);
			pmachine.jump(entry);
			while ((status = pmachine.execute(10000)) == PMachine::OPCOUNT_LIMIT) ;
			if (status == PMachine::OK && pmachine.stackPointer() != pmachine.stackBase()) {
				puts("Unbalanced stack pointer");
				return 1;
			}
		}
		Int result = pmachine.stackPointer()[0].i;
#else
		{
			for (j = 0; j < 200000; ++j) result = fibbc(fibbI);
		}
#endif

		clock_t c1 = clock();
		std::cout << status << std::endl;
		std::cout << result << std::endl;
		std::cout << static_cast<double>(c1 - c0) / CLOCKS_PER_SEC << std::endl;
	}
	{
		for (int i = 100000; i < 110000; ++i) {
			if (globals[i].i != 0xAACC5599) {
				puts("Corrupt data after globals");
				return 1;
			}
		}
	}
	{
		std::string s;
		getline(std::cin, s);
	}
	return 0;
}

/*

for (j = 1; j < 20000; ++j) if (array[j] < array[minj]) minj = j;

reg machine

		IMM16 r0 #1
		IMM16 r1 #0
		IMM16 r9 #test
		IMM16 r10 #1
		IMM16 r11 #notl
		IMM16 r12 #20000
		IMM16 r13 #2
		IMM16 r14 #end
test	LTi r2 r0 r12
		NOT r2 r2
		GOIF r14 r2
		ADDi r2 r13 r0
		ADDi r3 r13 r1
		GET r2 r2
		GET r3 r3
		LTf r2 r2 r3
		NOT r2 r2
		GOIF r11 r2
		SET r1 r0
notl	ADDi r0 r0 r10
		GOTO r9
		


stack machines

		SETi	mini	#0
		SETi	i		#1
		GOTO	test
loop	GETf	ADDi	array	GETi	mini
		GETf	ADDi	array	GETi	i
		GOIF	notl	NOT		LTf
		SETi	mini	GETi	i
notl	SETi	i		ADDi	GETi	i		#1
test	GOIF	loop	LTi		GETi	i		#20000
		GETi	mini
		RET

		SETi	i		#1
		SETi	mini	#0
		GOTO	test
loop	GETfADDi2	GETi	mini
		GETfADDi2	GET0
		GOIF	notl	NOT		LTf
		SETi	mini	GET0
notl	SETi	i		ADDi	GET0		#1
test	GOIF	loop	LTi		GET0		#20000
		GETi	mini
		RET

		SETi	mini	#0
		#1
		GOTO	test
loop	GETf	ADDi	array	DUPi
		GETf	ADDi	array	GETi	mini
		GOIF	notl	LTf
		SETi	mini	DUPi
notl	ADDi	#1
test	GOIF	loop	LTi		SWPi	#20000	DUPi
		POPi
		GETi	mini
		RET

		SETi mini #0+array
		#1+array
		GOTO test
loop		GOIF notl (LTf (GETf (GETi mini)) (GETf DUPi))
				SETi mini DUPi
notl		ADDi #1
test	GOIF loop (LTi (SWPi (#20000+array DUPi)))
		POPi
		SETi mini (ADDi #-2 (GETi mini))
		RET


		SETI seed 2463534242
		GOTO fill

myrand	\ XOR (SHFT SWPi -13) DUPi (GETi seed)
		\ XOR (SHFT SWPi 17) DUPi
		SETi seed DUPi (XOR (SHFT SWPi -5) DUPi)
		GOTO SWPi
		
fill	SETi idx (ADDi array #0)
begin	GOIF end (NOT LTi (GETi idx) (ADDi array #20000))
			SETf idx (TOf CALL myrand)
			SETi idx (ADDi (GETi idx #1))
		GOTO begin
end		EXIT
		

		\ SAVE XOR (SHFT LOAD -13) GETi y
		\ SAVE XOR (SHFT LOAD 17)
		\ SAVE XOR (SHFT LOAD -5)
		SETi seed

		SETi y XOR (SHFT GETi y -13) GETi y
		SETi y XOR (SHFT GETi y 17) GETi y
		SETi y XOR (SHFT GETi y -5) GETi y
		SETi seed y
		
		
		UInt myrand() {
			static UInt seed = 2463534242U;
			UInt y = seed;
			y ^= y << 13;
			y ^= y >> 17;
			seed = y ^ (y << 5);
			return seed;
		}


	
@rand
	$t = $seed << 13
	$seed = $seed ^ $t
	$t = $seed >> 17
	$seed = $seed ^ $t
	$t = $seed << 5
	$seed = $seed ^ $t
	%ret. = Float $seed
	%ret. = %ret. * 2.3283064370808e-10
	%ret. = %ret. * %0.
	return
	
@randRange
	$d. = %1. - %0.
	call @rand $d.
	%ret. = %ret. + %0.
	return
	
@fill
	$i = 0
	for $i < 20000
		call @rand 1234.0
		&array.[$i] = %ret.
	return
	



@rand
	SHL $t, $seed, #13
	XOR $seed, $seed, $t
	SHR $t, $seed, #17
	XOR $seed, $seed, $t
	SHL $t, $seed, #5
	XOR $seed, $seed, $t
	TOf %R, $seed
	MULf %R, %R, #2.3283064370808e-10
	MULf %R, %R, %0
	RET
	
@randRange
	MOVE $min, %0
	SUBf %0, %1, $min
	CALL @rand
	ADDf %R, %R, $min
	RET

@fib
	EQi @noRecurse, %0, #0
	EQi @noRecurse, %0, #1
		PUSH $fibN_1, %0
		SUBi $fibN_1, %0, #1
		CALL @fib, $fibN_1
		MOVE $fibN_1, %R
		SUBi $fibN_1, %0, #1
		CALL @fib, $fibN_1
		ADDi %R, $fibN_1, %R
		POP %0, $fibN_1
		RET
	@noRecurse
		MOVE %R, %0
		POP $fibN_1
		RET


function fib is:
input: integer n such that n >= 0

    1. if n is 0, return 0
    2. if n is 1, return 1
    3. otherwise, return [ fib(n-1) + fib(n-2) ]

end fib


@test
	SWITCH @label, $x
		DEFAULT @label
		CASE @label, 0
		CASE @label, 1
		CASE @label, 10
		CASE @label, 20

*/

/*
	function rec $a $b $c $d $e
	
	$a=$a
	$b=$d
	$c=$b    bang

	call rec $a $d $b $e $c

*/

/*
	GLOB array 20000
	GLOB test1
	GLOB test2
	
	FUNC findSmallestIndex
		IN PTR array, INT count
		OUT INT mini, FLT minv
		VARS INT i, FLT v
	
		MOVE i, #1
		MOVE mini, #0
		PEEK minv, array, 0
		loop:
			PEEK v, array, i
			GEQ v, minv, @notLess
				MOVE minv, v
				MOVE mini, i
			@notLess:
		FORU i, count, loop
		# Implicit RET here
		

converts to (with Pika or whatever)
 |
 v
		
FUNC findSmallestIndex
IN PTR array count
OUT INT mini FLT minv 
VAR INT i FLT v
MOVE i #1
MOVE mini #0
PEEK minv array mini
loop:
PEEK v array i
GEQ v minv notLess
MOVE minv v
MOVE mini i
notLess:
FORU i count loop

compiles to
 |
 v
  
ENTR 6
MOVEc 0 #1
MOVEc 2 #0
PEEK 3 4 2
loop:
PEEK 1 4 0
GEQf 1 3 notLess
MOVE 3 1
MOVE 2 0
notLess:
FORU 0 5 loop




	FUNC test
		VAR INT count, PTR array[10000]
		
		MOVE %A0, array
		MOVE %A1, count
		CALL findSmallestIndex
		MOVE result, %R0
		
	FUNC fibb
		IN INT n
		OUT INT fibn
		VARS INT n1, INT n2
		
		if n <= #2 then @one
			n1 = n - 1
			n2 = n - 2
			fibn1 = fibb(n1)
			fibn2 = fibb(n2)
			fibn = fibn1 + fibn2
			return fibn
		@one
			return #1
			
	
	FUNC processMidi
		ARGS INT count, PTR messages
		VARS INT i, INT status, INT data0, INT data1, INT cmd
		
		if count == 0 then @done
			i = 0
			@loop1:
				msg = [messages + i]
				splitMidiMessage(msg, status, data0, data1)
				cmd = status & 0xF0
				switch @cmdSwitch cmd
					case @cmdSwitch 0x90:
						if data1 == 0 then @noteOff
						doNoteOn(data0, data1)
						goto @doneSwitch
					
					case @cmdSwitch 0x80:
					@noteOff:
						doNoteOff(data0, data1)

				@doneSwitch:
			foru i < count do @loop1
		@done:

	FUNC splitMidiMessage
		ARGS I32 message
		RETS I32 channel, I32 cmd, I32 data0, I32 data1
		
		AND cmd, message, #0xF00000
		AND channel, message, #0x0F0000
		AND data0, message, #0x00FF00
		AND data1, message, #0x0000FF
		SHR cmd, cmd, #24
		SHR channel, channel, #16
		SHR data0, data0, #8
		RET
		
		
	FUNC process
		VARS I32 i, F32 thisMessage
		
		EQU inputCount, #0 -> @done
			MOVE #0 -> outputCount
			@loop1:
				PEEK inputMessages, i -> thisMessage
				CALL splitMidiMessage, thisMessage -> status, data0, data1
				AND status, #0xF0 -> cmd
				SHR cmd, #8 -> cmd
				SWCH cmd -> @cmdSwitch
					// default:
						POKE thisMessage -> outputMessages, outputCount
						ADD outputCount, #1 -> outputCount
						GOTO @doneSwitch
						
					@cmdSwitch#0x9:
						EQU data1, #0 -> @noteOff
						CALL doNoteOn, data0, data1
						GOTO @doneSwitch
					
					@cmdSwitch#0x8:
					@noteOff:
						CALL doNoteOff, data0, data1

				@doneSwitch:
			FORU i inputCount -> @loop1
		@done:
		
process:
	FUNC	4	2	4
	EQUi	l0	a0	0
	MOVE	v0	0
	MOVE	a2	0
l1	PEEK	c3	a1	v0
	CALL	splitMidiMessage
	ANDi	v1	c0	240
	SHRi	v1	v1	4
	SUBi	v1	v1	8
	SWCH	l2	v1	2
	POKE	a3	a2	c3
	ADDi	a2	a2	1
	GOTO	l4
l2	EQUi	l3	c2	0
	CALL	doNoteOn
	GOTO	l4
l3	CALL	doNoteOff
l4	FORU	l1	v0	a0
l0	RETU


	-------------
	Call return 0		Call return 0		Call arg 0
	Call return 1		Call arg 0			Call arg 1
	Call return 2		Call arg 1			Call arg 2
	Call arg 0			-					Call arg 3
	Call arg 1			-					Call arg 4
	Call arg 2			-					Call arg 5
	-					-					Call arg 6
	Local var 0
	Local var 1
	Local var 2
	Return 0
	Return 1
	Arg 0
	Arg 1
	------------

	no ...
	
	-------------
	-					-				Call arg 0
	Call return 0		-				Call arg 1
	Call return 1		-				Call arg 2
	Call return 2		-				Call arg 3
	Call arg 0			Call return 0	Call arg 4
	Call arg 1			Call arg 0		Call arg 5
	Call arg 2			Call arg 1		Call arg 6
0:	Local var 0
	Local var 1
	Local var 2
	Return 0
	Return 1
	Arg 0
	Arg 1
	------------
	
	no ...
	
	------------
	
	A0
	A1
	V0
	V1
	V2
0:	C0
	C1
	C2
	C3
	C4
	
*/
