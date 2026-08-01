# Runtime `match` form and `match/case` layout

## Status

Design settled; implementation pending. This is the user-facing **`match`**
expression (match/case style), distinct from **`switch`** / equality dispatch
(`compile/switch`). It is **Phase 8** of the matcher rollup
([matcher-ng-rollup.md](matcher-ng-rollup.md)). Phases 1-6 (native engine,
builder API, frozen-tuple constants, subject adapters — including the runtime
`PySequence_Fast` adapter — and the surface matcher parser) are landed, so the
matching machinery this form drives already exists.

## Summary

- Add a **`match`** form used like **match/case** rather than **switch/case**.
- Add a **reader layout** for **match/case**, parallel in role to `layout/switch`
  (`koskari/reader/layouts/conditionals.py`).
- Each **arm** carries **matcher syntax** — the same surface pattern language the
  Phase 6 parser (`parse_matcher` / `META_MATCHER` in `koskari/matcher.py`)
  already lowers to a frozen matcher tuple.
- **Compilation** mirrors `compile/switch`: evaluate the subject once, keep it on
  the stack across the arm chain, and emit a per-arm branch opcode. Two new
  opcodes carry the work: **`MATCH_CASE`** (test + branch, analogous to
  `SWITCH_EQ`) and **`MATCH_STORE_NAMES`** (bind the committed arm's captures).

## Opcodes

### `MATCH_CASE`

Style: `LABEL` with an `EXTRA` row, the same schema family as
`SWITCH_EQ_VAR_ATTR`.

The stack contract mirrors `SWITCH_EQ`: the subject is evaluated once and left on
the stack; the opcode runs one arm's frozen matcher against it.

- **No match:** leave the subject on the stack; **jump** to the arm's label (the
  next arm's prelude, or a shared else / none label).
- **First match:** pop the subject; **deposit the `MatchResult`** on the stack;
  fall through into the arm's binding and body.

EXTRA row fields:

- **matcher const** — pool index of the frozen 6-tuple for this arm's pattern.
- **externs** — tuple of names to load from the local environment and pass as
  extern bindings (classes / types / symbolic comparison values the pattern
  references). Discovered from the frozen matcher's `externs` list at compile
  time, loaded from the namespace at execution, validated at match entry.
- **label** — branch target on no-match.
- **cache slot** — index of the per-site `KOS_CACHE_MATCHER` slot (below).

Each arm re-runs its matcher, which re-adapts the subject on entry
(`PySequence_Fast` is near-free when the subject is already a list/tuple). The
rollup's adapt-once dynnode strategy (§"Dynchain / Runtime Match Case Strategy")
is a deferred optimization, not part of the first cut.

### Matcher constant and the `KOS_CACHE_MATCHER` cache slot

The arm pattern lowers to a frozen **6-tuple** matcher constant
(`("MATCHER", slots, externs, consts, nodes, entry)`) embedded in the code
constant pool. The tuple is plain, serializable data, which keeps the codex /
`.kio` path simple.

Because the const stays a tuple, `MATCH_CASE` must **not** re-thaw it on every
execution. It carries a per-site cache slot holding the thawed native `Matcher`,
using the existing `KosCacheSlot` framework (`koskari/lib/bytecode.h`), which is
built to grow new kinds: "a future field-accessor, jump-target, or exception-type
cache adds its own kind and its own union member; it never reshapes an existing
one."

Add `KOS_CACHE_MATCHER` as a new `KosCacheKind`. It is the **simplest** kind in
the set. The method and field caches carry a `type` guard, a `type_version`, and
a `misses` / megamorphic bypass because their target can change under them; a
thawed matcher cannot — the thaw is a pure function of the const tuple, so once
the slot is populated it is permanently valid for that site.

- **Union member:** one owned strong ref to the `Matcher`; no guard, no version,
  no miss counter.
- **Populate:** first hit reads the tuple const, calls `Matcher.from_tuple`, and
  stashes the result in the slot.
- **Teardown:** decref the matcher — an owning arm, like `KOS_CACHE_METHOD`, not
  a no-op like the ref-free `KOS_CACHE_FIELD`.
- **GC:** visit the one ref.
- **Reentrancy / threads:** a race yields two thaws and one winner; the frozen
  matcher is immutable, so the discarded duplicate is harmless.

Each arm is its own site with its own matcher const and its own cache slot.

### `MATCH_STORE_NAMES`

Style: takes a constant tuple of names. Runs immediately after a matched
`MATCH_CASE` falls through, consuming the `MatchResult` from TOS.

For each name in the tuple it binds that capture into the arm namespace **by
name**, popping the result when done. One opcode does the whole arm's binding in
one dispatch, which is deliberately cheaper than emitting N individual store
opcodes (N dispatches plus N result fetches). The tradeoff is that the body reads
those names as namespace (`VAR`) loads.

The name tuple is the arm pattern's **full declared slot set**, so the body
compiles against a known local set regardless of which optionals fired. The
handler owns two policies:

- **Sparse captures:** `MatchResult.captures` is sparse — a slot an `OPTIONAL`
  arm did not bind is absent from the dict. `MATCH_STORE_NAMES` stores `ABSENT`
  (the `koskari.lib.ABSENT` singleton) for any declared name missing from the
  result, so every name the body can reference is bound to something.
- **Datum reveal:** single-cell captures come back wrapped in `koskari.lib.Datum`
  (read through `.datum`). The handler does a one-level reveal: a `Datum` yields
  its `.datum`, anything else passes through.

An arm with no captures skips `MATCH_STORE_NAMES` and emits a bare `POP 1` to
discard the result.

## Lowering sketch

Modeled on `special_switch` (`koskari/compile/specials/conditionals.py`):

```text
1. Evaluate subject once           -> stack [..., s]
2. For each arm:
     parse arm pattern -> frozen matcher tuple (const pool)
     collect externs from matcher.externs
     MATCH_CASE (const, externs, L_next, cache_slot)
       no match: [..., s], jump L_next
       match:    pop s, push result
     if arm binds names: MATCH_STORE_NAMES (names)   # pops result, binds
     else:               POP 1                        # discard result
     <arm body>          # helper_begin, sequence context
     JUMP L_done
   L_next: [..., s] persists into the next arm
3. else / no match:
     else present  -> POP 1; <else body>
     else absent   -> POP 1; CONST None
   L_done:
```

The subject occupies one stack slot across the whole chain, exactly as the
discriminant does in `compile/switch`.

## Surface syntax

`match/case` layout, parallel to the `switch` block (`layout_switch`):

```scheme
match SUBJECT:
  case PATTERN:
    BODY...
  case PATTERN:
    BODY...
  else:
    BODY...
```

Each `case` PATTERN is matcher surface syntax (Phase 6). Lowered form:

```scheme
(compile/match SUBJECT
  (PATTERN BODY...)
  (PATTERN BODY...)
  (else: BODY...))
```

No-match with no `else:` yields `None`, matching `compile/switch` and
`compile/cond`.

## Dependency posture

The **seq-values-kwargs** foundation
([seq-values-kwargs-formals-apply.md](seq-values-kwargs-formals-apply.md)) would
generalize arm binding into a runtime application ABI, but it is **not** on the
critical path and must not block this work. `MATCH_STORE_NAMES` binds the arm's
compile-time-known slot set directly. seq-values-kwargs remains a later
optimization and generalization, not a prerequisite.

## Open decisions

- **Repeat / REST capture binding shape.** Repeat-frame captures come back as
  bare Python lists and `REST` slices as `Datum`-of-slice or tuples, so the
  one-level reveal does not fully unwrap them. What a repeat or rest slot binds to
  in the arm body (the raw list? unwrapped cells?) is deferred until real patterns
  exercise it; simple captures land first.
- **Arm scope isolation.** Whether each arm's bound names are isolated to that arm
  (and whether any binding leaks past the whole `match` form) determines the
  namespace `MATCH_STORE_NAMES` targets. Settle before the special is final.
- **`CLASS` shorthand.** Inherited from the rollup (decision 6): the Phase 6
  parser recognizes only canonical `(CLASS ...)`, not `(Point x: 0 y)` sugar.

## Implementation order

1. **Opcodes and cache kind.** `MATCH_CASE`, `MATCH_STORE_NAMES` in
   `koskari/lib/opcodes.incl`; per-opcode impls under `koskari/lib/opcode/`;
   regenerate `handlers.incl`. `KOS_CACHE_MATCHER` in `koskari/lib/bytecode.h`
   with its union member, teardown, and GC arms. Opcode tests modeled on
   `tests/lib/opcode/test_switch.py`.
2. **Reader layout.** `layout/match` in
   `koskari/reader/layouts/conditionals.py`, parallel to `layout_switch`,
   emitting `(compile/match SUBJECT (PATTERN BODY...) ...)`.
3. **Compile special.** `compile/match` in
   `koskari/compile/specials/conditionals.py`: parse each arm pattern through the
   matcher Phase 6 parser to a frozen tuple const, collect externs, emit the
   `MATCH_CASE` chain, per-arm `MATCH_STORE_NAMES` / `POP`, bodies, and the else /
   none tail. Register the builtin.
4. **Tests.** Opcode-level, special-level (`tests/compile/specials/`), and
   full-stack reproducer scenarios; corpus records once the surface stabilizes.

## Related

- [matcher-ng-rollup.md](matcher-ng-rollup.md) — unified matcher; Phase 8 is this
  form. §"Dynchain / Runtime Match Case Strategy" and §"Runtime Matching vs Syntax
  Matching" cover the engine-side view.
- [../archive/switch-eq-opcodes.md](../archive/switch-eq-opcodes.md) — `SWITCH_EQ`
  design this mirrors.
- [seq-values-kwargs-formals-apply.md](seq-values-kwargs-formals-apply.md) — the
  deferred generalized-binding foundation.
- [../LAYOUTS.md](../LAYOUTS.md) — layout conventions.
