/*
	JitEscapeAnalysis - corpus measurement for the v2 register-allocation design decision in
	docs/JitCompilerResearch.md (section 1.1 / 5.7): "escape floor" (ADRL is the thief) vs.
	"barrier" (the pointer *use* is the thief) register caching.

	For every FUNC in every .gazl file given (or found in given directories), this tool:
	  - reconstructs the local frame layout (declaration order = layout order),
	  - computes the escape floor = the lowest layout offset among all ADRL targets and
	    GETL/SETL bases (every slot at or above the floor is "aliasable", below it "private"),
	  - finds loops (backward branches) and whether their innermost loop contains a CALL,
	  - counts operand accesses to scalar locals, split private/aliasable and cold/hot.

	The money metric is "hot aliasable scalar accesses in call-free loops": accesses that the
	escape-floor design forces to memory but a barrier design would keep in registers (loops
	*with* calls flush at the call safepoint anyway, so barriers win little there).

	This is a text-level analysis of the assembly (not a full assembler); it mirrors the
	operand grammar closely enough for measurement purposes.
*/
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Local {
	std::string name;
	int offset;						// allocation offset in declaration order (words)
	int size;						// words
	bool isArray;					// LOCA / PARA
	bool isParam;					// INP* / OUT* / PARA
	bool aliasable;					// offset >= escape floor
};

struct Insn {
	std::string op;
	std::vector<std::string> operands;
	bool inLoop;
	bool inCallFreeLoop;			// innermost containing loop has no CALL
};

struct FuncStats {
	std::string file, name;
	int nScalars, nArrays, nAliasScalars;
	bool hasFloor;
	int floor;
	long privCold, privHot;			// accesses to private scalars
	long aliasCold, aliasHotCallFree, aliasHotWithCall;
	long arrayAccesses;
	int adrlScalar, adrlArray, adrlParam, adrlTransient, adrlNonZeroSize;
	int getlSetl;
	long insnCount;
};

static bool isDataDirective(const std::string& op) {
	return op == "GLOB" || op == "CNST" || op == "TEMP" || op == "DATA"
			|| op == "DATi" || op == "DATf" || op == "DATp" || op == "DATs";
}

static bool isDeclScalar(const std::string& op) {
	return op == "LOCi" || op == "LOCf" || op == "LOCp"
			|| op == "INPi" || op == "INPf" || op == "INPp"
			|| op == "OUTi" || op == "OUTf" || op == "OUTp";
}

static bool isDeclParam(const std::string& op) { return op[0] == 'I' || op[0] == 'O' || op == "PARA"; }

/// Strips a trailing `:offset` suffix; returns the numeric offset if it parses, else 0.
static std::string baseName(const std::string& operand, int& suffixOffset) {
	suffixOffset = 0;
	size_t colon = operand.find(':');
	if (colon == std::string::npos) return operand;
	suffixOffset = std::atoi(operand.c_str() + colon + 1);
	return operand.substr(0, colon);
}

static bool isLocalRef(const std::string& operand) {
	if (operand.empty()) return false;
	char c = operand[0];
	return std::strchr("#&@%^*<\"", c) == 0;
}

/**
	Parses one file's worth of lines into per-function stats.
**/
struct Analyzer {
	std::vector<FuncStats> results;

	// current function state
	bool inFunc = false;
	FuncStats cur;
	std::vector<Local> locals;
	std::map<std::string, int> localByName;
	int allocSize = 0;
	std::vector<Insn> insns;
	std::map<std::string, int> labelAt;			// label -> index of next insn
	std::vector<std::pair<int, int>> floorRefs;	// (offset, insnIdx) of ADRL targets / GETL-SETL bases

	void finishFunc() {
		if (!inFunc) return;
		// escape floor
		cur.hasFloor = !floorRefs.empty();
		cur.floor = 0x7FFFFFFF;
		for (size_t i = 0; i < floorRefs.size(); ++i) cur.floor = std::min(cur.floor, floorRefs[i].first);
		for (size_t i = 0; i < locals.size(); ++i) {
			locals[i].aliasable = cur.hasFloor && locals[i].offset >= cur.floor;
			if (locals[i].isArray) ++cur.nArrays;
			else { ++cur.nScalars; if (locals[i].aliasable) ++cur.nAliasScalars; }
		}
		// loops: backward branches -> ranges; innermost = smallest containing range
		struct Range { int lo, hi; bool hasCall; };
		std::vector<Range> ranges;
		for (size_t b = 0; b < insns.size(); ++b) {
			for (size_t o = 0; o < insns[b].operands.size(); ++o) {
				const std::string& t = insns[b].operands[o];
				if (t.size() < 2 || t[0] != '@') continue;
				std::map<std::string, int>::iterator it = labelAt.find(t.substr(1));
				if (it != labelAt.end() && it->second <= (int)b) {
					Range r; r.lo = it->second; r.hi = (int)b; r.hasCall = false;
					for (int k = r.lo; k <= r.hi; ++k) {
						if (insns[k].op == "CALL") { r.hasCall = true; break; }
					}
					ranges.push_back(r);
				}
			}
		}
		for (size_t i = 0; i < insns.size(); ++i) {
			int best = 0x7FFFFFFF;
			bool in = false, callFree = false;
			for (size_t r = 0; r < ranges.size(); ++r) {
				if ((int)i < ranges[r].lo || (int)i > ranges[r].hi) continue;
				in = true;
				int len = ranges[r].hi - ranges[r].lo;
				if (len < best) { best = len; callFree = !ranges[r].hasCall; }
			}
			insns[i].inLoop = in;
			insns[i].inCallFreeLoop = in && callFree;
		}
		// accesses
		for (size_t i = 0; i < insns.size(); ++i) {
			const Insn& n = insns[i];
			++cur.insnCount;
			for (size_t o = 0; o < n.operands.size(); ++o) {
				if (n.op == "ADRL" && o == 1) continue;											// address-taking, not a data access
				if (!isLocalRef(n.operands[o])) continue;
				int suffix = 0;
				std::map<std::string, int>::iterator it = localByName.find(baseName(n.operands[o], suffix));
				if (it == localByName.end()) continue;
				const Local& l = locals[it->second];
				if (l.isArray) { ++cur.arrayAccesses; continue; }
				if (l.aliasable) {
					if (!n.inLoop) ++cur.aliasCold;
					else if (n.inCallFreeLoop) ++cur.aliasHotCallFree;
					else ++cur.aliasHotWithCall;
				} else {
					if (n.inLoop) ++cur.privHot; else ++cur.privCold;
				}
			}
		}
		results.push_back(cur);
		inFunc = false;
	}

	void startFunc(const std::string& file, const std::string& name) {
		finishFunc();
		inFunc = true;
		cur = FuncStats();
		cur.file = file;
		cur.name = name;
		locals.clear(); localByName.clear(); allocSize = 0;
		insns.clear(); labelAt.clear(); floorRefs.clear();
	}

	void addFloorRef(const std::string& operand) {
		if (operand.empty()) return;
		if (operand[0] == '%') { ++cur.adrlTransient; return; }										// transient target: report only
		if (!isLocalRef(operand)) return;
		int suffix = 0;
		std::map<std::string, int>::iterator it = localByName.find(baseName(operand, suffix));
		if (it != localByName.end()) floorRefs.push_back(std::make_pair(locals[it->second].offset + suffix, (int)insns.size()));
	}

	void line(const std::string& file, std::vector<std::string>& tokens) {
		size_t t = 0;
		std::string label;
		if (t < tokens.size() && tokens[t].size() > 1 && tokens[t][tokens[t].size() - 1] == ':') {
			label = tokens[t].substr(0, tokens[t].size() - 1);
			++t;
		}
		if (t >= tokens.size()) return;
		const std::string& op = tokens[t];
		std::vector<std::string> operands(tokens.begin() + t + 1, tokens.end());

		if (op == "FUNC") { startFunc(file, label.empty() ? "(anonymous)" : label); return; }
		if (!inFunc) return;
		if (isDataDirective(op)) { finishFunc(); return; }											// data section ends the function scan
		if (isDeclScalar(op) || op == "LOCA" || op == "PARA") {
			Local l;
			l.name = label;
			l.offset = allocSize;
			l.size = 1;
			l.isArray = (op == "LOCA" || op == "PARA");
			if (l.isArray && !operands.empty() && operands[0][0] == '*') l.size = std::atoi(operands[0].c_str() + 1);
			l.isParam = isDeclParam(op);
			l.aliasable = false;
			if (!l.name.empty()) localByName[l.name] = (int)locals.size();
			locals.push_back(l);
			allocSize += l.size;
			return;
		}
		// executable instruction
		if (!label.empty()) labelAt[label] = (int)insns.size();
		if (op == "ADRL" && operands.size() >= 2) {
			const std::string& target = operands[1];
			int suffix = 0;
			std::map<std::string, int>::iterator it = localByName.find(baseName(target, suffix));
			if (target[0] == '%') ++cur.adrlTransient;
			else if (it != localByName.end()) {
				const Local& l = locals[it->second];
				if (l.isParam) ++cur.adrlParam;
				else if (l.isArray) ++cur.adrlArray;
				else ++cur.adrlScalar;
			}
			if (operands.size() >= 3 && operands[2][0] == '*' && std::atoi(operands[2].c_str() + 1) != 0) ++cur.adrlNonZeroSize;
			addFloorRef(target);
		} else if (op == "GETL" && operands.size() >= 2) { ++cur.getlSetl; addFloorRef(operands[1]); }
		else if (op == "SETL" && operands.size() >= 1) { ++cur.getlSetl; addFloorRef(operands[0]); }
		Insn n;
		n.op = op;
		n.operands = operands;
		n.inLoop = n.inCallFreeLoop = false;
		insns.push_back(n);
	}
};

static void stripCommentAndTokenize(const std::string& lineIn, std::vector<std::string>& tokens) {
	tokens.clear();
	bool inString = false;
	std::string token;
	for (size_t i = 0; i < lineIn.size(); ++i) {
		char c = lineIn[i];
		if (c == '"') inString = !inString;
		if (c == ';' && !inString) break;
		if ((c == ' ' || c == '\t' || c == '\r') && !inString) {
			if (!token.empty()) { tokens.push_back(token); token.clear(); }
		} else token += c;
	}
	if (!token.empty()) tokens.push_back(token);
}

int main(int argc, const char* argv[]) {
	std::vector<fs::path> files;
	for (int a = 1; a < argc; ++a) {
		fs::path p(argv[a]);
		if (fs::is_directory(p)) {
			for (fs::directory_iterator it(p), e; it != e; ++it) {
				if (it->path().extension() == ".gazl") files.push_back(it->path());
			}
		} else files.push_back(p);
	}
	if (files.empty()) {
		std::printf("usage: JitEscapeAnalysis <dir-or-file.gazl> ...\n");
		return 1;
	}
	std::sort(files.begin(), files.end());

	Analyzer az;
	for (size_t f = 0; f < files.size(); ++f) {
		std::ifstream in(files[f]);
		if (!in) { std::printf("cannot open %s\n", files[f].string().c_str()); return 1; }
		std::string lineText;
		std::vector<std::string> tokens;
		std::string shortName = files[f].filename().string();
		while (std::getline(in, lineText)) {
			stripCommentAndTokenize(lineText, tokens);
			if (tokens.empty() || tokens[0][0] == '!') continue;
			az.line(shortName, tokens);
		}
		az.finishFunc();
	}

	// ---- corpus rollup ----
	long fTotal = 0, fWithFloor = 0;
	long scalars = 0, aliasScalars = 0;
	long privCold = 0, privHot = 0, aliasCold = 0, aliasHotCF = 0, aliasHotWC = 0, arrayAcc = 0;
	long adrlScalar = 0, adrlArray = 0, adrlParam = 0, adrlTransient = 0, adrlNZ = 0, getlSetl = 0;
	for (size_t i = 0; i < az.results.size(); ++i) {
		const FuncStats& r = az.results[i];
		++fTotal;
		if (r.hasFloor) ++fWithFloor;
		scalars += r.nScalars; aliasScalars += r.nAliasScalars;
		privCold += r.privCold; privHot += r.privHot;
		aliasCold += r.aliasCold; aliasHotCF += r.aliasHotCallFree; aliasHotWC += r.aliasHotWithCall;
		arrayAcc += r.arrayAccesses;
		adrlScalar += r.adrlScalar; adrlArray += r.adrlArray; adrlParam += r.adrlParam;
		adrlTransient += r.adrlTransient; adrlNZ += r.adrlNonZeroSize; getlSetl += r.getlSetl;
	}
	long scalarAcc = privCold + privHot + aliasCold + aliasHotCF + aliasHotWC;
	std::printf("files: %d   functions: %ld   functions with an escape floor: %ld (%.0f%%)\n",
			(int)files.size(), fTotal, fWithFloor, 100.0 * fWithFloor / (fTotal ? fTotal : 1));
	std::printf("scalar locals: %ld   of which aliasable under the floor: %ld (%.1f%%)\n",
			scalars, aliasScalars, 100.0 * aliasScalars / (scalars ? scalars : 1));
	std::printf("ADRL targets: scalar %ld / array %ld / param %ld / transient %ld   (nonzero *size: %ld)\n",
			adrlScalar, adrlArray, adrlParam, adrlTransient, adrlNZ);
	std::printf("GETL+SETL sites: %ld\n\n", getlSetl);
	std::printf("scalar-local accesses: %ld total\n", scalarAcc);
	std::printf("  private (register-cached either way):   cold %-7ld hot %ld\n", privCold, privHot);
	std::printf("  aliasable (memory under escape floor):  cold %-7ld hot(call-free loop) %ld   hot(loop w/ CALL) %ld\n",
			aliasCold, aliasHotCF, aliasHotWC);
	std::printf("  -> pessimized-hot share of all scalar accesses: %.1f%% (call-free) + %.1f%% (w/ CALL)\n",
			100.0 * aliasHotCF / (scalarAcc ? scalarAcc : 1), 100.0 * aliasHotWC / (scalarAcc ? scalarAcc : 1));
	std::printf("array accesses (memory either way): %ld\n\n", arrayAcc);

	// ---- top offenders ----
	std::vector<const FuncStats*> order;
	for (size_t i = 0; i < az.results.size(); ++i) order.push_back(&az.results[i]);
	std::sort(order.begin(), order.end(), [](const FuncStats* a, const FuncStats* b) {
		if (a->aliasHotCallFree != b->aliasHotCallFree) return a->aliasHotCallFree > b->aliasHotCallFree;
		return a->aliasHotWithCall > b->aliasHotWithCall;
	});
	std::printf("top functions by hot aliasable scalar accesses (escape-floor pessimization):\n");
	std::printf("  %-28s %-16s %6s %6s %8s %8s %8s\n", "file", "function", "scal", "alias", "hotCF", "hotCall", "cold");
	for (size_t i = 0; i < order.size() && i < 20; ++i) {
		const FuncStats& r = *order[i];
		if (r.aliasHotCallFree == 0 && r.aliasHotWithCall == 0) break;
		std::printf("  %-28s %-16s %6d %6d %8ld %8ld %8ld\n", r.file.c_str(), r.name.c_str(),
				r.nScalars, r.nAliasScalars, r.aliasHotCallFree, r.aliasHotWithCall, r.aliasCold);
	}
	return 0;
}
