# Dict Literal Compression: a Theoretical `compile/#dict` Form

**Status: EXPLORATORY (2026-07-19). The dedicated `compile/#dict`
special was removed (56d77346) in favor of the uniform call lowering;
this document records the compression methods a future compile-time
form could apply, and the identification problem any of them must
solve first. Nothing here is scheduled.**

## Current state

The `#{}` literal is reader sugar over an ordinary call. The reader
lowers each `KEY : VALUE` / `keyword:` entry into a
`(#tuple KEY VALUE)` invocation form (`_split_dict_keywords`,
`reader/parser.py`), so the whole literal becomes a plain
`(#dict entry...)` call. `**:` splats and value-position keywords pass
through untouched; a spine the matcher cannot parse passes through
unchanged so the `#dict` call reports the malformed form on its own
terms.

Runtime `#dict` is `build_dict` (`KosUtils_BuildDict`, `utils.c`):
each positional argument is a (key, value) pair -- a Koskari pair or
any 2-sequence -- inserted in order via `PyDict_SetItem` (later key
wins), then call kwargs merge on top (a keyword in a *direct call* is
a string key; a keyword key in the *literal* is re-read as the key
expression before lowering, so by the call form the two surfaces
agree).

Call compilation const-folds constant `(#tuple k v)` entries into
pooled tuples, so a fully-constant literal already costs one pooled
CONST per entry plus the outer call.

**The invariant that killed the old special and constrains any new
one: one semantics.** `(#dict ...)` written directly, applied at
runtime, and produced by the literal all mean the same call. The
previous `compile/#dict` special (BUILD_KWDS/COMBINE_KWDS assembly)
was removed because maintaining that equivalence across every entry
shape made the construction disproportionately complicated. Any
revival must be a *provably equivalent lowering of the call form*,
not a second semantics.

## What the current form costs

For `#{a: 1, b: 2, c: 3, d: 4}` (constant keys and values):
4x CONST (pooled entry tuples) + 1 call into `build_dict`, which then
walks each tuple through `PySequence_GetItem` twice per entry. For
computed values each entry additionally pays a real `(#tuple k v)`
call (callable deref, invoke, tuple build) before the outer call.

The cost is therefore two-part: dispatch/call overhead at the
production site, and the generic pair-walking loop inside
`build_dict`. Compression methods attack both.

## The identification problem (gate before any method)

The compiler sees a call whose head is the *symbol* `#dict`. Fusing it
assumes that symbol resolves to the builtin at runtime -- but bindings
are late-bound and shadowable, and the one-semantics invariant means
the literal erases nothing: there is no "literal origin" left in the
form. Options, in rough order of preference:

1. **Resolution-proven binding.** Fuse only when compile-time
   resolution can prove the head is the bootstrap `#dict` (global
   binding, module sealed, no intervening shadow). Strongest
   semantics; depends on resolution machinery being able to make that
   promise (today it generally cannot, for runtime-rebindable
   globals).
2. **Pragma gate.** A `compiler/fuse-dict-literals` (naming TBD)
   pragma under which the module author asserts `#dict` means the
   builtin -- the same trust model as `tco_enabled`. Cheap, honest,
   opt-in; the default stays uncompressed.
3. **Reader-origin marker.** The literal could mark its emitted call
   form (position metadata or a distinguished head) so only
   literal-origin calls fuse. Rejected as primary: it reintroduces
   exactly the two-surfaces divergence the call lowering removed --
   a shadowed `#dict` would then behave differently between the
   literal and a written call.

Whichever gate is chosen, the fusion site is **form-level, inside
call compilation** (where the head symbol and the literal
`(#tuple ...)` argument shapes are visible before emission), not a
late row sweep -- by emission time the head is an anonymous callable
deref and the shape is gone.

## Compression methods

### A -- fully-constant literal: template copy

When every entry is `(#tuple const const)`, the entire dict is a
compile-time constant. Pool a *template* (either the tuple-of-pairs,
or a pre-built dict) and emit one instruction:

- **Pooled pairs + one call**: CONST(pooled tuple of pairs) +
  `build_dict` call -- already nearly what const-folding produces;
  saves only the per-entry CONSTs. Minor.
- **`BUILD_KWDS_CONST`** (mirroring `BUILD_LIST_CONST`): arg = pool
  index of a pre-built template dict; the handler pushes
  `PyDict_Copy(template)`. One dispatch, no pair walking at all.
  The template lives only in the pool and is never exposed, so
  mutability is confined to the copy. This is the cheapest possible
  form for constant config-style literals.

Duplicate keys in the literal collapse at compile time exactly as
they would at runtime (later wins) -- the template is built by the
same insertion order.

### B -- constant keys, computed values: `BUILD_KWDS_NAMES`

The method this document exists to record. `BUILD_KWDS_NAMES` already
exists (CONST arg = pooled tuple, pops N stack values, zips
`names[i] -> value[i]` into a fresh dict) and its handler is
**key-agnostic** -- nothing restricts the tuple to strings, any
hashable constants serve. So:

```
#{a: x, b: (f y), c: 3}      ; keys constant, values computed
  =>  push x, push (f y), push 3
      BUILD_KWDS_NAMES  c#K  ; K = ("a", "b", "c")
```

N value pushes + one dispatch, versus N `#tuple` entry productions +
the outer call + the runtime pair walk. Values evaluate left-to-right
in entry order (identical to the call form); constant keys evaluate
nothing (evaluating a constant has no effects, so eliding it is
sound); later-wins duplicate handling matches dict-from-zip insertion
order.

### C -- mixed literals: run splitting + `COMBINE_KWDS`

Entries partition into runs of constant-key entries and
computed-key/splat entries. Lower each constant-key run via method B,
each computed run via `BUILD_KWDS` (PAIR_COUNT over key/value stack
pairs -- the opcode survives and is exactly this shape), and each
`**:` splat as its mapping expression; then merge the run results in
order with `COMBINE_KWDS` (COUNT). In-order merging preserves both
entry evaluation order and later-wins semantics across run
boundaries.

This is where the old special's complexity lived, and the reason to
demand corpus data before building it: if real literals are
overwhelmingly all-constant-keys (method B) or fully constant
(method A), the mixed machinery may never earn its weight. opgrams
over the corpus and real application code (`#{}` usage in scheduler,
koslife, mcp) should decide.

### D -- splat-only and degenerate shapes

`#{**: m}` alone is `dict(m)` -- a single-mapping copy; barely worth
special-casing beyond what C provides. Empty `#{}` could become a
bare `BUILD_KWDS 0` (fresh empty dict, one dispatch) under any gate.

## Equivalence obligations (all methods)

- Entry evaluation order: left-to-right, key before value within an
  entry (const keys trivially satisfy the key side).
- Duplicate keys: later wins, including across method-C run
  boundaries.
- Error surfaces: only well-formed literal shapes fuse; anything
  irregular (malformed spine, keyword in value position) falls back
  to the plain call and errors on the call's own terms, unchanged.
- Unhashable constant keys: method A/B would raise at *pool build /
  template build* time (compile) where the call raises at runtime.
  Either accept the earlier error as an improvement or restrict
  constant keys to known-hashable const types; decide at
  implementation.

## Sequencing sketch (when picked up)

1. Decide the gate (resolution-proof vs pragma). Everything hangs on
   this.
2. Method A's `BUILD_KWDS_CONST` template copy -- smallest surface,
   biggest win for constant literals, one new opcode with
   `BUILD_LIST_CONST` as precedent.
3. Method B via the existing `BUILD_KWDS_NAMES` -- no new opcodes.
4. Method C only with corpus evidence of mixed-literal volume.

## Non-goals

- Changing `build_dict` runtime semantics or the `#{}` reader
  lowering -- both stand as the uniform base the compression must be
  equivalent to.
- Dict *comprehension* forms; separate feature entirely.

<!-- The end. -->
