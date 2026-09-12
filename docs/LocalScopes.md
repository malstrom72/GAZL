# Local scopes: names, not just slots

Status: **DESIGN, targeting GAZL 2 + Impala 3.0.** The allocation half - `SCOP` / `ENDS` placing an
expansion's locals - already ships on this branch and is quoted from the source below. The naming half,
which is what this document proposes, does not exist anywhere yet. Every GAZL fragment shown as
*proposed* output is hand-written illustration, not compiler output; the fragments quoted as *today's*
behaviour are from `src/GAZL.cpp` on this branch and from `impala/testdata/`.

The question that started it: why must GAZL locals be declared at the top of a `FUNC`, and what would it
take to declare them anywhere - inside a true lexical scope, the way a C block does it?

## 1. What this branch has today

`SCOP` and `ENDS` bracket a group of declarations whose lifetimes do not overlap the next group's
(`src/GAZL.cpp:1156-1172`):

	case SCOP____:	if (ip != (functionStart + 1)) throw Exception(MUST_DEFINE_LOCALS_FIRST);
					if (localScopeDepth >= MAX_LOCAL_SCOPE_DEPTH) throw Exception(UNBALANCED_LOCAL_SCOPE);
					localScopeStack[localScopeDepth++] = localsSize;
					break;

	case ENDS____:	if (ip != (functionStart + 1)) throw Exception(MUST_DEFINE_LOCALS_FIRST);
					if (localScopeDepth == 0) throw Exception(UNBALANCED_LOCAL_SCOPE);
					localsSize = localScopeStack[--localScopeDepth];
					break;

`ENDS` rewinds the bump, so a **sibling** group reuses the same offsets while a **nested** group stacks on
top. The frame is `maxLocalsSize` - the peak over nesting chains, not the sum (`src/GAZL.cpp:875`). No
instruction is emitted and the VM is unchanged: this is purely a declaration-time construct.

Note both cases carry the same `ip != (functionStart + 1)` guard as `LOCA____` itself
(`src/GAZL.cpp:1146`). **Scopes are prologue-only.** They group declarations; they do not bracket code.

## 2. Why locals must come first

At run time `FUNC` bumps the data stack pointer by the whole frame at once, and every variable operand
indexes off it:

	case FUNC_CC_:	if ((dsp += (UInt)(C0.i)) + C1.i > dataStackEnd) { ... }		// src/GAZL.cpp:1311
	#define V0 (dsp[ip->p0.i])												// src/GAZL.cpp:1284

So `dsp` sits at the **top** of the frame. Locals are negative offsets from it; transients (`%n`, the
outgoing call window) are non-negative. The assembler assigns a local its offset from a running counter,
then rebases every reference at the moment it parses it (`src/GAZL.cpp:855`):

	linkWithOffset(locals, b, e, accepts, v);
	v->i -= localsSize;

That subtraction uses `localsSize` **as it stands right now**. It is correct only because no further
locals can be declared. Declare one halfway down a body and every operand encoded before it was biased
against a smaller frame - each one silently off by a few words, with no relocation record to repair it.
`finalizeFunction` only stamps the totals into the `FUNC` header; it holds no list of emitted operands.

`MUST_DEFINE_LOCALS_FIRST` is therefore not a taste decision. It is the guard that keeps that miscompile
unreachable, and it is checked against `ip` rather than a flag, so the real rule is *"before the first
emitted instruction"*. Forward branch labels are fine (they are code offsets, patched by
`resolveForwardRefs`); locals are not.

The sign convention is also load-bearing for the calling convention. At a call the caller does
`dsp += C1.i` to open its transient window, then the callee's `FUNC` adds its own frame - so the callee's
**first-declared** local lands exactly on the caller's window base. Declaration order is the ABI. That is
why the deepest slot is the first declared, and why the bias must be against the final total.

The one line that makes `SCOP` / `ENDS` fit all this costs nothing (`src/GAZL.cpp:1175-1178`):

	if (ip == functionStart + 1) {			// leaving the declaration region
		if (localScopeDepth != 0) throw Exception(UNBALANCED_LOCAL_SCOPE);
		localsSize = maxLocalsSize;			// every local operand and RETU below resolves against the FINAL frame
	}

`localsSize` does double duty: a running bump during the prologue, then snapped to the final frame size
before any operand can be parsed. Scoping is bought for a stack of `UInt`s and zero relocation machinery
- **because** declarations stay in the prologue.

## 3. What is missing

`ENDS` rewinds the offset counter. It does not touch the symbol table, and `Symbols::define` rejects a
duplicate outright (`src/GAZL.cpp:607`):

	if (it != symbols.end()) throw Exception(SYMBOL_ALREADY_DEFINED, name);

So every name in a `FUNC` must still be unique. Two sibling scopes cannot both declare `$tmp`. For the
inline expander that is handled by renaming - `var tag = '_i' + (inlineCounter++)`
(`impala/impalaCompiler.js:1629`), appended to every label and local an expansion replays. For
hand-written GAZL, and for an Impala programmer who wants `i` in two sibling loops, there is no remedy at
all.

## 4. Name scoping forces scopes to bracket code

This is the hinge, and it is worth stating plainly because it decides the whole cost.

In the prologue-only arrangement, every scope closes before the first instruction. If `ENDS` also erased
its names, then every local declared inside a scope would be unreferenceable from anywhere - the only
code that exists is below the last `ENDS`. Name scoping is meaningless unless a scope **contains
instructions**.

So the moment names go out of scope, `LOC*` has to be legal after code, `MUST_DEFINE_LOCALS_FIRST` goes
away for real, and the invariant from section 2 - that `localsSize` is final before any operand is parsed
- goes with it.

## 5. What that costs in the assembler

**The bias fixup.** Emit local operands unbiased (the raw `declOffset`, positive from the frame base),
record where they went, apply the bias once at `finalizeFunction`:

	// parseOperand, default: - replaces `v->i -= localsSize`
	linkWithOffset(locals, b, e, accepts, v);
	localFixups.push_back(v);				// cleared per function, like forwardRefs

	// finalizeFunction, before `functionStart->p0.i = maxLocalsSize`
	for (it = localFixups.begin(); it != localFixups.end(); ++it) (*it)->i -= maxLocalsSize;
	localFixups.clear();

`forwardRefs` already establishes the pattern of deferring an operand write to finalize, so this is in
keeping. A no-allocation variant exists - emit `declOffset - SENTINEL` and re-walk the function's own
instructions at finalize - but it needs an opcode-to-variable-operand-mask table to know which operand
slots to inspect, so the vector is the cheaper build.

**`RETU` carries the frame size.** `p0` is stamped at emit time (`src/GAZL.cpp:1213`). The interpreter
never reads it, but it would be wrong for any return placed before the last declaration. Fold it into the
same finalize pass.

**`LOCAL_BOUNDS` reads the sign.** This is the subtle one (`src/GAZL.cpp:1236`):

	if ((op->otherFlags & LOCAL_BOUNDS) != 0)
		paramsSize = maximum((Int)(paramsSize), p1->i + p2->i);

It works today *because* of the sign convention: a local's biased offset lies in `[-localsSize, -1]`, so
`p1->i + p2->i` can never exceed 0 and only a transient can grow the outgoing window. Unbiased, a local's
`p1->i` is positive and every `ADRL` of a local would inflate `paramsSize`. The check has to consult the
fixup marker, or move to finalize.

**Erasure must filter on `BRANCH`.** Branch labels live in the same `locals` map and must stay
function-wide, so a `GOTO` can leave a scope and so forward refs survive across `ENDS`. Recording
per-scope name lists alongside `localScopeStack` handles it: at `ENDS`, erase that scope's variable names
only.

**Branching into a scope.** With code-bracketing scopes and slot overlay, a `GOTO` from outside into the
middle of a scope lands on slots holding a sibling scope's leftovers. Still in-frame, so the memory-safety
model is not violated - just stale, and a stale *pointer* is the nasty case. Cheap to reject: tag each
`BRANCH` symbol with its scope path and refuse a link whose target scope is not an ancestor-or-self of the
reference site.

**One thing is already safe.** Variable operand types do not carry `FORWARD` - only `FUNC`, `NATIVE` and
`FWD_BRANCH` do - so `Symbols::link` throws on an unknown variable name rather than parking a forward ref
(`src/GAZL.cpp:649`):

	if ((accepts & FORWARD) == 0) throw Exception(NON_FORWARD_SYMBOL_NOT_FOUND, name);

Erasing names at `ENDS` therefore produces a **loud** error at every out-of-scope reference. There is no
path where a stale reference silently re-resolves to a sibling scope's redefinition of the same name -
which is the miscompile class this feature would otherwise invite.

## 6. What it does, and does not do, for `inline`

Worth recording honestly, because "it will simplify the expander" is the easy assumption.

**It removes the local half of `_i<N>`.** An expansion could replay `$tmp` verbatim inside its own `SCOP`:
no counter for the variable half, collision-freedom by construction rather than by discipline, and
readable names in disassembly. Inline-within-inline nests naturally, where the suffix scheme needs
uniqueness across the whole expansion tree.

**It does not remove the label half.** Labels must stay function-wide: an expansion with an early exit
jumps to a label after its own `ENDS`, and forward refs resolve at `finalizeFunction` by name. So `.f0_i3`
and the "tag before the `#`" rule for `.s0_i12#0` survive. Scoping labels too is strictly harder than
scoping variables - variables are non-forwardable, so erasing them is safe by construction; labels are
forward-referenced by design and would need refs to propagate outward at `ENDS`.

**It does not remove the extent constants.** See section 8: those live in the global namespace, which no
frame scoping reaches.

**It does not remove the substitution walk.** Argument substitution already rewrites every operand, so
renaming rides along in a pass that exists regardless. This deletes a string concat, not a pass.

**Codegen is bit-identical.** `maxLocalsSize` overlay already gives the slot reuse. Names have no runtime
existence.

**And it does not touch what made `inline` correct.** The parked rationale is that the transient-based
lowering re-implemented allocation numerically and broke whenever an extent was not a number Impala knows.
`SCOP` / `ENDS` fixed that by delegating placement to the assembler, which resolves `*.z.Struct` from
host-supplied sizes. The prologue-only version delivers that in full; name scoping adds nothing to it.

**Conclusion: do not justify this feature by `inline`.** Justify it by hand-written GAZL and by Impala
block locals, and judge the cost against those.

## 7. Impala syntax

Impala declares locals in a header clause today - `function test() locals int i, array mydata[N] { ... }`
- which mirrors the GAZL prologue exactly. Block scoping means admitting the same clause as a statement:

	function main()
	locals array buffer[1024]
	{
		for (i = 0 to 4) {
			locals int array tmp[16];
			fill(tmp);
		}
		{
			locals int array tmp[64];
			drain(tmp);
		}
	}

`LocalsDecl` is reused verbatim - it already parses the comma list of `VarDecl / ArrayDecl`. The only
grammar change is admitting it as a `Statement` alternative with a `;`, which is what Impala already
demands of every simple statement. The header form keeps no terminator because it is a clause, not a
statement.

**Keep the `locals` keyword; do not go bare `int i;`.** A struct type is an `Identifier`, so `Filter f;`
and `f = 1;` are distinguishable only by knowing which names are struct atoms. The compiler does know, but
that is C's declaration-vs-expression ambiguity, and a typo'd identifier would start reporting "Unknown
type" - precisely the agent-hostile corner the declarator grammar was designed against.

Proposed rules:

1. **Legal anywhere a statement is**, visible from the declaration to the enclosing `}`. No block-top
   restriction: it costs a positional rule and a diagnostic to enforce and buys nothing at the GAZL level,
   since the fixup is needed either way. Use-before-declare stays loud on both sides.
2. **A block emits `SCOP` / `ENDS` only if it declares something.** Empty scopes are free but noisy.
3. **Sibling reuse allowed, shadowing rejected**, with a new code from the diagnostic registry. Writing
   `i` in two sibling loops is the entire point; shadowing an enclosing `i` buys nothing and hides the
   assign-to-the-wrong-one bug class.
4. **`goto` into a block that declares locals: rejected**, pairing with the assembler-side check in
   section 5. Impala labels are function-wide, so nothing stops such a jump being written today; the
   rejection is what keeps the two levels telling one story.
5. No runtime consequences: slots are uninitialized on entry now and stay that way, so a scope re-entered
   by a loop behaves exactly as it does today.

Implementation note: `symbols.locals` is a flat map keyed `'$' + name` and pruned per function, so it needs
a scope stack of its own, and `claimTopName`'s one-kind-per-top-level-name rule has to be taught what reuse
means.

## 8. The symbol namespace

An array's extent is an assemble-time constant in the **global** namespace - `! DEFi` declares into
`globals` (`src/GAZL.cpp:1083`) - so frame scoping cannot reach it. Two sibling blocks each declaring `tmp` would mint the same
constant twice and stop the assembler with `SYMBOL_ALREADY_DEFINED`, a long way from the cause.

This is *not* a reason to exclude arrays from scoping, and it does not need a new mangling scheme, because
the extent symbol is already a qualified path (`impala/impalaCompiler.js:2020`):

	extentSymbol = function (name, owner) {
		return '.x.' + (owner !== undefined ? owner + '.' : '') + name;
	};

A **local** array already passes the function as owner, so `array buffer[1024]` in `main` is already
`.x.main.buffer`, as `impala/testdata/inputTest.expected.gazl:12` shows. Adding a scope component extends
that path by one element:

	extentSymbol = function (name, owner, scope) {
		return '.x.' + (owner !== undefined ? owner + '.' : '')
					 + (scope !== undefined ? scope + '.' : '')
					 + name;
	};

`scope !== undefined`, not a truthiness test - the first inner scope is ordinal `0` and would otherwise
vanish. Function level passes `undefined`, so **every symbol emitted today keeps its exact current name**
and no fixture moves. The feature is purely additive.

Illustrating the proposed output for the Impala source in section 7:

	main:				FUNC
										PARA *1
						$i:				LOCi
	.x.main.buffer:		! DEFi #1024					; function level - unchanged
						$buffer:		LOCA *.x.main.buffer
	;-----------------------------------------------------------------------------
						...loop head...
						SCOP
	.x.main.0.tmp:		! DEFi #16
						$tmp:			LOCA *.x.main.0.tmp
						...fill(tmp)...
						ENDS
						SCOP
	.x.main.1.tmp:		! DEFi #64
						$tmp:			LOCA *.x.main.1.tmp
						...drain(tmp)...
						ENDS
										RETU

Two namespaces, two mechanisms, and that is the point: `$tmp` declared twice is the GAZL frame scoping
from section 4, while `.x.main.0.tmp` / `.x.main.1.tmp` is the global namespace, which frame scoping never
reaches.

**Flat ordinals, not nested paths.** Number scopes with a per-function counter in source order, so a scope
nested three deep is still one component (`.x.main.7.tmp`, not `.x.main.0.1.2.tmp`). Unique either way,
and flat matches how `labelCounter` already numbers `.f0` / `.s0` per function. The ordinal is identity,
not structure.

**Soundness is the argument already in `SymbolNamespace.md`.** The scope component is a NUMBER where a
name would be an identifier, so it can never collide with a user name - the same reasoning that lets a
numeric component sit inside a layout-constant path. The docs change is one row in that file's inventory
saying `<path>` may carry a scope ordinal between owner and name. No new tag letter, no suffix namespace,
and no interaction with `claimTopName`, since the owner component already does that work.

**Cost, and it is small.** Scope ordinals are source-order-derived, so inserting a block renumbers later
ones and churns emitted symbol names in that function's `.gazl`. That is the characteristic the compiler
already accepts for `labelCounter` and `.f0` / `.s0`, so it is not a new class of problem. A
churn-minimizing variant is to number the *reoccurrences of the reused name* rather than the scopes -
since shadowing is rejected, a name is only ever reused across siblings - but `.x.main.2.tmp` reads better
in disassembly because it names the block.

### Two branch-state notes

- **This branch still emits `.x.`** for array extents. Impala 2 retired the `.x.` tag and let `.z.` absorb
  every extent, so after the next merge from `Impala2` the symbols above read `.z.main.0.tmp`. The design
  is tag-agnostic: the scope component goes in the same position either way.
- **`.d.` axis constants do not exist here yet.** They arrive with the same merge, from the
  multidimensional-array work. `axisSymbol` takes the identical insertion - scope after the owner, axis
  still last, e.g. `.d.main.0.tmp.0`. Those two numeric components sit at different positions and stay
  decodable, because the component before a name can only be a scope ordinal while a name can never be a
  number. Nothing parses these paths back today - they are minted and compared by value - but it is worth
  keeping them decodable.

## 9. Open decisions

- Whether to lift `MUST_DEFINE_LOCALS_FIRST` at all. Everything in sections 5 to 8 is contingent on it;
  the prologue-only design in section 1 already delivers what `inline` needs, at zero cost.
- Whether a local array's extent constant is ever visible outside its function. No signature row appears to
  be emitted for one - those are for globals and struct fields - which would make the renumbering churn
  purely cosmetic. **Confirm before committing to source-order ordinals**; if `gazl-validate` ever compares
  a local extent across units, the churn matters considerably more.
- Whether labels get scoped too. Section 6 argues they should not, but that is the decision that would let
  an expansion drop `_i<N>` entirely.
- Which diagnostic codes the shadowing rejection and the branch-into-scope rejection take, and whether the
  latter belongs in the assembler, in `gazl-validate`, or both.

## See also

- [`docs/SymbolNamespace.md`](SymbolNamespace.md) - the tag inventory and the one-kind-per-top-level-name rule
- [`docs/ParkedFeatures.md`](ParkedFeatures.md) - why `inline` needs GAZL 2, and what went with it
- [`docs/Inlining.md`](Inlining.md) - the expansion machinery this would simplify, and by how little
- [`docs/MemorySafetyModel.md`](MemorySafetyModel.md) - why an overlaid slot is stale but never unsafe
- [`docs/TwoStageConstants.md`](TwoStageConstants.md) - why an extent must stay a symbol
