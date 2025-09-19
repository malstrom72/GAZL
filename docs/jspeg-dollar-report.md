# JSPEG `$$` Container Semantics

## Summary
- JSPEG rewrites bare `$$` tokens in actions to `$._` but now recognises `$$.` as an escape hatch that emits the holder `$`, optionally preserving a following property access, so grammars can reach the container directly when needed. 【F:impala/jspeg/jspeg.jspeg†L80-L110】
- The `Variable` tokenizer mirrors the new escape hatch by mapping `$$.` prefixes to holder-qualified names (for example `$$.tag` → `$.tag`) while leaving bare `$$` bound to the value slot. 【F:impala/jspeg/jspeg.jspeg†L204-L207】
- The generated parser function still creates an `_o={_:void 0}` holder and only returns the `._` field to callers, so host code cannot yet observe or replace the full container. 【F:impala/jspeg/jspeg.jspeg†L19-L27】

## Current Implementation Details

### Action Rewriting
The `Action` production strips the surrounding braces from each action block, walks the remaining source, and performs token-by-token rewrites. When it sees `$$.` it now emits the holder `$` (re-inserting the dot when a property follows), while bare `$$` tokens still rewrite to `$._` so value-oriented grammars keep working unchanged. 【F:impala/jspeg/jspeg.jspeg†L80-L110】

### Variable Tokenisation
The `Variable` rule recognises both `$$.` and bare `$$`. The former now rewrites to holder-qualified names (for example `$$.tag` becomes `$.tag`), and the latter still yields `'$._'`. This keeps captures, tags, and action rewrites in sync so any `$$.` prefix targets the container consistently. 【F:impala/jspeg/jspeg.jspeg†L204-L207】

### Parser Entry Point
The top-level `root` rule emits the generated parser wrapper. It materialises the shared container `_o={_:void 0}` and returns `_o._` as the semantic value, hiding the holder from callers entirely. Any future attempt to expose the container needs to adjust this wrapper in tandem with the `$$` rewrites. 【F:impala/jspeg/jspeg.jspeg†L19-L27】

## Limitations Compared to PPEG
The PPEG runtime allows host code to pass a container by reference (`@result`) when invoking a generated parser, which means grammars and callers can cooperate on the complete structure behind `$$`. 【F:impala/jspeg/PPEG.md†L53-L66】 JSPEG’s new `$$.` escape restores that in-grammar access, but the generated wrapper still hides the holder by returning only `_o._`, so callers cannot yet supply or retrieve the container the way PPEG does. 【F:impala/jspeg/jspeg.jspeg†L19-L27】

## Architecture Adjustment Ideas

The following ideas remain relevant for future evolution. Option 7 below is now implemented and documents the shipped `$$.` holder escape hatch for reference.

### 1. Introduce an Explicit Container Alias Inside Actions
Add a second token in the `Variable` rule—e.g. `$$$` or `$container`—that rewrites to the bare `$` holder instead of `$._`. The action walker would allow property access on this alias without appending `._`, giving grammar authors a deliberate way to reach the container when needed while keeping existing `$$` behaviour intact. Implementation touches would include extending the `Variable` alternation and adjusting the `Action` rewrite branch that handles the `$$` prefix. 【F:impala/jspeg/jspeg.jspeg†L73-L107】【F:impala/jspeg/jspeg.jspeg†L189-L191】

*Pros:* preserves backwards compatibility for grammars that rely on the value-only semantics; keeps the holder concept internal to JSPEG. *Cons:* introduces yet another sigil to document and remember; actions must opt in everywhere they need the container.

### 2. Swap the Meaning of `$$` and Add a New Value Shorthand
Flip the mapping so that bare `$$` refers to the holder object and introduce an explicit value helper (for example `$$v`). The `Action` walker would stop appending `._` after every `$$`, and the new helper would take over the value rewrite logic. Generated parsers would still return `_o._`, but grammars could now use `$$` consistently for container operations just like PPEG’s `@$$`. 【F:impala/jspeg/jspeg.jspeg†L73-L126】【F:impala/jspeg/jspeg.jspeg†L19-L27】

*Pros:* Restores conceptual parity with the PPEG container model; existing grammars that only mutate the value slot could switch to the new helper via find-and-replace. *Cons:* Risky for existing JSPEG grammars—`$$` would suddenly become an object, so arithmetic or string actions would break until migrated.

### 3. Thread Parallel Holder and Value Variables
Keep the current `$$` rewrite but augment the generated parser with an additional variable that always references the live holder (e.g. `var $h = $;`). The action rewriter could surface this binding under a dedicated sigil (such as `$$holder`) without changing existing rewrites. Both the `Variable` rule and the `root` wrapper would need to initialise and return this extra binding in lock-step so advanced grammars or host code can obtain the container when necessary. 【F:impala/jspeg/jspeg.jspeg†L19-L27】【F:impala/jspeg/jspeg.jspeg†L73-L126】【F:impala/jspeg/jspeg.jspeg†L189-L191】

*Pros:* Avoids behavioural churn for existing grammars; exposes the holder to both actions and callers once they opt in. *Cons:* Slightly higher runtime overhead (another variable to maintain); grammar authors must learn the new alias and mind the distinction between holder and value variables.

### 4. Grammar-Scoped Holder Mode Toggle
Introduce a grammar directive (for example `%holderMode value|container`) that records whether a rule should expose the holder or just the value. The `Definition` rule already threads metadata like `tag`, `vi`, and the variable registry `vr` through `Expression`, `Sequence`, and `Action`, so the same plumbing can propagate a `holderMode` flag. When a rule opts into container mode the `Action` walker would stop forcing `$._` for bare `$$` references, while the default stays value-only. Implementation touches include extending the rule context initialisation in `Definition`, checking the flag inside the `Action` rewrite loop, and teaching the `Variable` rule to honour the same decision so tags and captures remain consistent. 【F:impala/jspeg/jspeg.jspeg†L46-L107】【F:impala/jspeg/jspeg.jspeg†L189-L191】

*Pros:* Gives grammar authors precise, opt-in control over semantics while keeping existing grammars stable; scopes the change to the rules that truly need full container access. *Cons:* Adds another directive to the grammar syntax and requires documentation to explain the interaction between rule-level modes.

### 5. Expose the Holder Through the Parser API
Change the generated wrapper so callers can supply and retrieve the holder object explicitly. Today the wrapper instantiates `_o={_:void 0}` and returns only `_o._`, hiding the container entirely. By adding an optional `holder` parameter and returning `_o` (or both `_o._` and `_o`) the host can regain the same reference-passing behaviour that `@result` provides in PPEG. Actions could still see just the value unless combined with another option, but host code would once again be able to observe and persist the accumulator structure. Implementing this requires updating the `root` rule’s emitted function signature and return tuple while keeping backwards-compatible defaults. 【F:impala/jspeg/jspeg.jspeg†L19-L27】

*Pros:* Restores parity for host integrations that expect to manage the container directly; minimal churn inside the action rewriter. *Cons:* Does not solve in-grammar access by itself; changing the generated function signature may require updating wrappers that call JSPEG parsers.

### 6. Add an Action Escape Hatch for Raw Holder Access
Extend the action rewriter with a deliberate escape hatch, such as recognising `` $$[holder] `` or a decorator like `@withHolder { ... }`. When the escape is present, the `Action` loop can splice the original source without rewriting bare `$$`, giving power users temporary raw access to `$` while the surrounding grammar keeps the value-centric behaviour. Because the loop already walks the action source token by token, the new construct can be detected before the existing `$$` rewrite executes. The `Variable` rule would need a matching form so tags and captures inside the escaped block agree on the holder semantics. 【F:impala/jspeg/jspeg.jspeg†L73-L126】【F:impala/jspeg/jspeg.jspeg†L189-L191】

*Pros:* Localised escape avoids broad semantic shifts and lets grammars opt in at the exact action that needs the holder. *Cons:* Adds syntactic noise and requires careful escaping logic to prevent the helper syntax from leaking into generated JavaScript.

### 7. Interpret `$$.` as the Container Holder *(Implemented)*
JSPEG now keeps the existing `$$`→`$._` rewrite while treating `$$.` as an escape hatch that emits the holder `$`. The action walker consumes the prefix, optionally re-inserting a dot so property accesses like `$$.foo` become `$.foo`, and the `Variable` rule maps `$$.` captures/tags to the same holder-aware form. The generated wrapper still returns `_o._`, preserving API compatibility for callers. 【F:impala/jspeg/jspeg.jspeg†L80-L110】【F:impala/jspeg/jspeg.jspeg†L204-L207】

*Pros:* Simple mental model—`$$` stays the value, `$$.` reaches the container—and required only a localised tweak to the rewrite logic. Existing grammars that already used `$$.` property chains now target the holder automatically. *Cons:* Callers still cannot obtain the holder, and authors must remember to include the dot when they want container semantics.

## Recommendation
With the `$$.` escape hatch in place, JSPEG once again lets grammars reach the holder without disturbing existing `$$` value semantics. Future exploration can focus on caller-facing improvements (such as exposing the holder through the parser API) or alternate ergonomics if container access needs to extend beyond actions.
