# Runtime `match` form and `match/case` layout

## Status

**Implemented (archived).** The runtime `match` expression — a
**sequence-unpacking** matcher, distinct from **`switch`** / equality
dispatch — is shipped as **Phase 8** of the matcher rollup
([matcher-ng-rollup.md](matcher-ng-rollup.md)). This doc is
the runtime / opcode-machinery record; the **surface pattern language**
(clause delimiter, head-driven classifier, reader-sugar heads, guards) is
in [match-pattern-language.md](match-pattern-language.md), whose model
supersedes the interim imply-`FORM` lowering described under "Design
principle" and "Surface syntax" below.

| Piece | Location |
| ----- | -------- |
| Opcodes | `koskari/lib/opcodes.incl` — `MATCH_CASE`, `MATCH_STORE_NAMES` |
| Handlers | `koskari/lib/opcode/MATCH_CASE.c`, `koskari/lib/opcode/MATCH_STORE_NAMES.c` |
| Cache kind | `koskari/lib/bytecode.h` — `KOS_CACHE_MATCHER` |
| Compiler special | `koskari/compile/specials/conditionals.py` — `compile/match` |
| Reader layout | `koskari/reader/layouts/conditionals.py` — `layout/match` |
| Tests | `tests/lib/opcode/test_match_case.py`; `tests/compile/specials/test_match.py`; `tests/reader/layouts/test_match_layout.py` |

## What it is

`match` destructures a value **as a sequence**. Each `case` supplies the
positional element-patterns of that sequence; the first arm whose pattern
matches binds its captures and runs its body. There is no whole-value / scalar
match — a case is always a sequence shape.

```scheme
match msg:
  case [(tag :: keyword) (id :: int) *: args]:
    (handle tag id args)
  case [a b]:
    (pair a b)
  else:
    (ignore)
```

## Design principle: the special makes no shape decisions

The special does **not** parse the pattern language. It **implies `FORM`** over
each case pattern and hands the whole form to the matcher's own surface parser:

```python
arm = MATCH(parse_matcher(cons(_symbol_form, pattern)))
```

`parse_matcher` classifies and lowers every element — captures, `::` typed
captures, literals, `CLASS`, nested sequences, `REST`. The only structural
checks the special performs are **`pair` and `nil`** (walking the clauses and
each body) plus the `else:` marker.

This is deliberate and load-bearing. **Brackets are visual aids only** — `()`
and `[]` are value-equal and compile identically, so the special must never
gate behavior on the cell being a bracket type. `[a b c]` and `(a b c)` are the
same value and lower the same way; a programmatically built pattern (macro,
quasiquote, `cons`) produces plain pairs and behaves identically. Implying
`FORM` and delegating to `parse_matcher` gets all of this for free.

Because `FORM` is implied, a case pattern's elements are its sequence positions.
A `::` type name lands as a matcher **extern**, not a capture; `[[greet :: str]
who]` matches a two-element sequence whose first element is a `str` bound to
`greet`.

### The `*:` rest rule lives in the matcher, not the special

`*: name` rest sugar is a rule of the **matcher surface**, added to the spine
composites so every matcher user benefits, not a match-only hack.
`form_factory` / `match_factory` in `koskari/matcher.py` lower their children
through a shared `_lower_spine_children`, which recognizes a `*:` keyword
followed by a slot name and emits `REST(name)`. So `(FORM a b *: rest)` —
which is what `[a b *: rest]` implies — yields `FORM(MATCH(CAPTURE a, CAPTURE b,
REST rest))`. The matcher-native spelling `(REST slot)` remains available.

### Externs are requested through resolution

A pattern's type / class / predicate names are resolved from the namespace at
**match time**. The special discovers them from the frozen matcher's `externs`
list and calls `fragment.request_var` on each, the same path a bare name
reference takes — otherwise a name bound in an enclosing scope and used only in
a pattern is never captured into the closure. It resolves fine as a global /
builtin regardless; the request is what makes closure capture work.

## Opcodes

### `MATCH_CASE`

Style: `LABEL` with an `EXTRA` row, the same schema family as
`SWITCH_EQ_VAR_ATTR`.

The stack contract mirrors `SWITCH_EQ`: the subject is evaluated once and left on
the stack; the opcode runs one arm's frozen matcher against it.

- **No match:** leave the subject on the stack; **jump** to the arm's label (the
  next arm's prelude, or a shared else / none label).
- **First match:** pop the subject; **deposit the capture dict** on the stack;
  fall through into the arm's binding and body.

EXTRA row fields:

- **matcher const** — pool index of the frozen 6-tuple for this arm's pattern.
- **externs** — tuple of names to load from the local environment and pass as
  extern bindings (classes / types / predicates the pattern references).
  Discovered from the frozen matcher's `externs` list at compile time, loaded
  from the namespace at execution, validated at match entry.
- **label** — branch target on no-match.
- **cache slot** — index of the per-site `KOS_CACHE_MATCHER` slot (below).

The subject is wrapped as a length-1 sequence on entry so the whole value
matches as one spine element. Each arm re-runs its matcher, re-adapting the
subject (`PySequence_Fast` is near-free when it is already a list/tuple); the
rollup's adapt-once dynnode strategy is a deferred optimization.

**Guard support (planned):** the 4th `EXTRA` slot (currently `NOARG`) becomes a
`keep`-subject flag. When set, a match does **not** pop the subject — it leaves
it on the stack and pushes the captures — so a `when` / `unless` guard can run
after binding and either advance to the next arm (subject intact) or `POP` the
subject and commit. See
[match-pattern-language.md](match-pattern-language.md) §Guards.

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

`KOS_CACHE_MATCHER` is the **simplest** kind in the set. The method and field
caches carry a `type` guard, a `type_version`, and a `misses` / megamorphic
bypass because their target can change under them; a thawed matcher cannot — the
thaw is a pure function of the const tuple, so once the slot is populated it is
permanently valid for that site.

- **Union member:** one owned strong ref to the `Matcher`; no guard, no version,
  no miss counter.
- **Populate:** first hit reads the tuple const, calls `KosMatcher_FromTuple`,
  and stashes the result in the slot.
- **Teardown:** decref the matcher — an owning arm, like `KOS_CACHE_METHOD`, not
  a no-op like the ref-free `KOS_CACHE_FIELD`.
- **GC:** visit the one ref (an immutable matcher cannot close a cycle back to
  the code, so the code object stays GC-untracked).
- **Reentrancy / threads:** a race yields two thaws and one winner; the frozen
  matcher is immutable, so the discarded duplicate is harmless.

Each arm is its own site with its own matcher const and its own cache slot.

### `MATCH_STORE_NAMES`

Style: takes a constant tuple of names. Runs immediately after a matched
`MATCH_CASE` falls through, consuming the capture dict from TOS.

For each name in the tuple it binds that capture into the arm namespace **by
name**, popping the dict when done. One opcode does the whole arm's binding in
one dispatch, which is deliberately cheaper than emitting N individual store
opcodes (N dispatches plus N result fetches). The tradeoff is that the body reads
those names as namespace (`VAR`) loads.

The name tuple is the arm pattern's declared slot set
(`pattern_slot_names`), and the special `declare_var`s each so the body compiles
against them. The handler owns two policies:

- **Sparse captures:** `MatchResult.captures` is sparse — a slot an `OPTIONAL`
  arm did not bind is absent from the dict. `MATCH_STORE_NAMES` **skips** any
  name absent from the result, leaving it unbound; referencing an unbound
  optional in the body raises `NameError`. (There is no `ABSENT` sentinel; the
  idea was vestigial and dropped.)
- **Datum reveal:** single-cell captures come back wrapped in `koskari.lib.Datum`
  (read through `.datum`). The handler does a one-level reveal: a `Datum` yields
  its `.datum`, anything else passes through.

An arm with no captures skips `MATCH_STORE_NAMES` and emits a bare `POP 1` to
discard the dict.

## Lowering

Modeled on `special_switch` (`koskari/compile/specials/conditionals.py`), one
fragment, subject held on the stack across the arm chain:

```text
1. Evaluate subject once           -> stack [..., s]
2. For each arm:
     frozen = MatchCompiler().lower(MATCH(parse_matcher(cons(FORM, pattern))))
     request_var each name in frozen.externs
     MATCH_CASE (const, externs, L_next, cache_slot)
       no match: [..., s], jump L_next
       match:    pop s, push captures dict
     declare_var each capture name
     if arm binds names: MATCH_STORE_NAMES (names)   # pops dict, binds
     else:               POP 1                        # discard dict
     <arm body>          # helper_begin, sequence context
     JUMP L_done
   L_next: [..., s] persists into the next arm
3. else / no match:
     else present  -> POP 1; <else body>
     else absent   -> POP 1; CONST None
   L_done:
```

## Surface syntax

`match/case` layout (`layout_match`), parallel to the `switch` block
(`layout_switch`):

```scheme
match SUBJECT:
  case PATTERN:
    BODY...
  case PATTERN:
    BODY...
  else:
    BODY...
```

Lowered form (also written directly):

```scheme
(compile/match SUBJECT
  (PATTERN BODY...)
  (PATTERN BODY...)
  (else: BODY...))
```

Each `PATTERN` is the sequence's positional element-patterns; `FORM` is implied
over it. Element vocabulary (all handled by `parse_matcher`):

| Element | Meaning |
|---|---|
| `a` | bind this position |
| `5` `"s"` `foo:` `'sym` | literal; the position must equal it |
| `(a :: T)` | typed bind (T is a runtime extern) |
| `(:: T)` | type check, no bind |
| `(a val: 5)` | bind + literal |
| `(CLASS C f: 0 g)` | class pattern (canonical `CLASS` only) |
| `[ .. ]` / `( .. )` | nested sequence (equivalent spellings) |
| `*: r` | rest capture — matcher surface rule |

No-match with no `else:` yields `None`, matching `compile/switch` and
`compile/cond`.

## Dependency posture

The **seq-values-kwargs** foundation
([seq-values-kwargs-formals-apply.md](../language/seq-values-kwargs-formals-apply.md)) would
generalize arm binding into a runtime application ABI, but it was **not** on the
critical path. `MATCH_STORE_NAMES` binds the arm's compile-time-known slot set
directly. seq-values-kwargs remains a later optimization, not a prerequisite.

## Open items

- **Repeat / REST capture binding shape.** Repeat-frame captures come back as
  bare Python lists and `REST` slices as `Datum`-of-slice or tuples, so the
  one-level reveal does not fully unwrap them. What a repeat or rest slot binds to
  is settled empirically as real patterns exercise it.
- **`CLASS` shorthand.** Inherited from the rollup (decision 6): the parser
  recognizes only canonical `(CLASS ...)`, not `(Point x: 0 y)` sugar.
- **Adapt-once subject dynnode.** Each arm re-adapts the subject; the rollup's
  dynnode strategy is a deferred optimization.

## Related

- [match-pattern-language.md](match-pattern-language.md) — the surface pattern
  language (clause delimiter, head vocabulary, reader sugars, guards) and the
  universal shape-detection vision.
- [matcher-ng-rollup.md](matcher-ng-rollup.md) — unified matcher; Phase 8 is this
  form.
- [switch-eq-opcodes.md](switch-eq-opcodes.md) — `SWITCH_EQ`
  design the stack contract mirrors.
- [seq-values-kwargs-formals-apply.md](../language/seq-values-kwargs-formals-apply.md) — the
  deferred generalized-binding foundation.
- [../LAYOUTS.md](../LAYOUTS.md) — layout conventions.
