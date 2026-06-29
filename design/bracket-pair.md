# Koskari Design: Bracket-Pair Type and Call-Position Lint

## Status

Proposed. A new `bracket-pair` pair subtype that records `[ ]` origin, a
reader that dispatches to it, and an optional compiler check that flags a
bracket-pair reaching evaluated/operator position. A symmetric advisory
matcher style-hint is sketched as a later, separate phase.

## Summary

Today the reader builds a single `pair` type from either `( )` or `[ ]`; the
delimiters are interchangeable and purely a visual choice for the author. The
established convention is `( )` for call/syntax forms and `[ ]` for data forms,
so that non-calls stand out (`(let [[a 1] [b 2]] (+ a b))` reads at a glance as
"`a` is not being called with `1`").

We promote that convention to a *queryable* property without making brackets
required. `[ ]` reads into a **`bracket-pair`** — a subclass of `pair` that
carries the same fields and compiles identically, but knows its origin glyph.
This buys two things:

1. **Repr fidelity** — source forms can round-trip the originating delimiter.
2. **A syntax-error guard** — the compiler can optionally warn or error when a
   bracket-pair is used as an invocation, on the premise that a `[ ]` in call
   position was almost certainly not meant to be a function call.

With the guard switched off, a `bracket-pair` is just a `pair` and everything
compiles as before. Brackets remain recommended, never required.

## Motivation

- The `( )` vs `[ ]` distinction already carries author intent, but the intent
  is discarded at read time. Preserving it lets tooling (repr, the compiler, a
  future style linter / LSP) act on it.
- A `[ ]` that lands in operator position is a high-confidence mistake: the
  author signalled "data" but wrote it where a call will be emitted.
- The mechanism is cheap and additive — it does not change how any existing,
  correctly-written program reads, compiles, or evaluates.

## Facets (the conceptual model)

- **Subtype, not a separate type.** `bracket-pair` subclasses `pair`
  (`KosPairType`), exactly as `nil` already does (`tp_base = &KosPairType`,
  `koskari/lib/pair.c`). Same `head` / `tail` / `position` fields; position
  data works through inheritance.
- **Per-cell, not per-list.** Bracket-ness rides on each cons *cell*, not on the
  list as a whole. `(cdr [a 1])` yields a bracket-list `[1]`; `(cons 0 [a 1])`
  yields a paren cell whose tail is a bracket-list. This smearing under
  structural ops is expected and acceptable.
- **Repr by the leading cell.** A list renders its whole spine in the style of
  its *head* cell. A mixed list prints by its leading pair; the bracket-ness of
  tail cells lies dormant until that sublist is taken on its own (via `cdr`) and
  printed, at which point its own head cell governs. This makes "mixed
  delimiters in one rendering" impossible.
- **The empty list has its own bracket identity (revised).** *Superseded by the
  bracket-nil work below.* The original plan collapsed `[]` to the single `nil`
  singleton (no cell to carry the flag). We instead introduced a distinct
  **`bracket-nil`** singleton, so `'[]` and `'()` are now distinguishable by
  identity (`is`), while still comparing equal by value (`==`). See
  "Bracket-nil: parallel empty-list singletons" below.
- **Joining is fine.** Splicing a bracket-list into a paren list (or vice versa)
  is never blocked. It is a normal, supported operation.

## Rules (behavior)

### Reader

- `(` reads into `pair` (via `cons`); `[` reads into `bracket-pair` (via a
  `bracket-cons` variant). Dispatch happens at the existing delimiter macros in
  `koskari/reader/parser.py` (`_read_pair` already receives the opening `char`,
  so it knows which constructor to use).
- `{` remains its own reader path (`_read_thunk`) and is **not** a third pair
  style. The distinction is two-state: default (`pair`) vs data (`bracket-pair`).

### Compiler call-position guard

- The guard lives at a **single chokepoint**: the entry of
  `Compiler.compile_pair` (`koskari/compile/compiler.py`). A pair only reaches
  `compile_pair` because it is about to be evaluated as a form; quoted data
  never arrives there (`quote` lowers to a const). So the check is one predicate
  at the top of the method: *is `source_obj` a bracket-pair?*
- The check sits **above** the special-vs-combination split, so it fires on any
  bracket-pair in evaluated position — including bracket-wrapped specials like
  `[if x a b]`, not only plain calls like `[a 1]`. This is intentional and
  consistent with the convention (`[ ]` is never the right delimiter for a
  syntax form either). Moving the check down into the `compile/call` fallthrough
  branches would instead restrict it to plain combinations; we keep it at the
  entry.
- **`let`-style binding positions are immune for free.** `let` and friends
  destructure their bindings *as data* in their special handlers; `[a 1]` is
  consumed there and never handed to `compile_pair` as a form. A check living at
  `compile_pair` entry structurally cannot see it. No special-form cooperation
  is required — the dispatch boundary itself separates "a special wanted a
  bracket here" from "a bracket leaked into eval position."
- **Quote exempts data structurally.** `(cons 0 [a 1])` flags (the `[a 1]` is in
  operator position, about to emit a call to `a`); `(cons 0 '[a 1])` does not —
  `quote` never compiles its argument as a combination, yielding a paren pair
  whose `cdr` is a bracket-list.
- **Macro-quiet by construction.** Expansions built with `cons` / `list` are
  paren-style, so re-compiling a macro's output never false-positives; a
  bracket-pair *passed to* a macro is data to that macro and never reaches
  `compile_pair`.

### Guard modes

A simple compiler flag (pragma gating deferred until the pragma system is
fleshed out) selects one of three modes for the `compile_pair` path:

- **accept** — compile the bracket-pair exactly as a paren pair (current
  behavior; the guard is a no-op).
- **warn** — compile, but emit a diagnostic.
- **error** — reject with a `CompilerError`.

## Implementation steps

1. **`bracket-pair` C type.** *Done.* Subclasses `KosPairType` as `bracket-pair`
   in `koskari/lib/pair.c`, overriding `kos_repr` / `tp_repr` / `tp_str` to
   render `[ ]`; position handling inherits. **`bracket-cons`** is implemented as
   the variadic builder of bracket-pair cells (`KosBracketPair_Cons` /
   `KosBracketPair_ConsFromPtr`, module `bracket_cons`, koskari `bracket-cons`):
   identical to `cons` except cells are bracket-pairs and an empty build returns
   `bracket-nil`. The last argument is still the explicit tail, so the terminator
   flavor is the caller's choice (`(bracket-cons 1 2 nil)` vs
   `(bracket-cons 1 2 bracket-nil)`).
   - **Landmine: `CheckExact` truncation (audited in `pair.c`).** Exact-type
     checks (`KosPair_CheckExact`, `ob_type == &KosPairType`) silently truncate
     a list at the first bracket cell because a subclass fails `CheckExact`.
     **Done:** every `pair.c` site is migrated — spine walks and nested-list
     recursions to `KosPair_IsCell` (`Check && ! KosNilP`, so they traverse
     bracket cells but still stop at either empty-list terminator), and the two
     `pair_eq` `ob_type != &KosPairType` checks to `! KosPair_Check` (so
     comparing non-empty bracket-pairs no longer recurses infinitely). This
     fixes repr/str/kos_repr, `follow`/`unpack`, `length`, `is-proper`,
     `__copy__`, position walks, and equality for bracket spines. The same
     migration is applied in `matcher.c` (the proper-list spine walk → `IsCell`;
     the position-cell guards → `Check`) and the `PAIR_SPLICE` opcode (segment
     guard → `Check`), so the matcher and quasiquote-splice walk either pair
     flavor. **By design the matcher does not distinguish bracket from paren** —
     telling them apart (the call-position lint) is a separate post-matcher pass
     gated by the pragma/flag, not the matcher's job. `pair_copy` traverses
     correctly but normalizes copied cells to plain pairs (bracket-style is not
     preserved through copy yet; revisit with `bracket-cons`).
   - **Landmine: free-list allocation.** `KosPair_New` pulls from a
     `&KosPairType` free-list. Bracket-pairs need their own allocation path (or
     their own free-list); they cannot reuse the base free-list as-is.
2. **Reader dispatch.** *Done.* `_read_pair` (`koskari/reader/parser.py`)
   dispatches on the opening char: `[` builds `bracket-pair` cells terminated by
   `bracket-nil` (empty `[]` → `bracket-nil`), `(`/`{` build plain `pair` cells
   terminated by `nil`. Position handling inherits. A `Reader`/`LayoutReader`
   `bracket_pairs` flag (default `True`) gates this — `bracket_pairs=False` makes
   `[ ]` read as plain pairs/nil, identical to `( )` (the flag is forwarded to
   `LayoutReader._inner`, which actually handles `[`).
   - **Python `is nil` audit (required by this step).** Because `[ ]` is used
     pervasively in source, terminating bracket lists with `bracket-nil` meant
     every raw `x is nil` identity check (≈139 across `compiler`, `helpers`,
     `hygiene`, `matcher`, `syntax`, and the specials/layouts) had to become
     `is_nil(x)` (the `KosNilP`-backed predicate) — otherwise list-walks over a
     bracketed form (e.g. `let`'s `[[a 1] [b 2]]`) would not terminate. All pair
     *checks* were already `isinstance`-based (`is_pair`, `CONST_TYPES` /
     `_PYCONS_DIRECT`), so bracket-pair *cells* needed no change — only the
     terminator predicate. `codex.py`'s pool-serialization `val is nil` is left
     exact (a `bracket-nil` const raises "unsupported" rather than silently
     serializing as `nil`; proper `.kio` support is a follow-up).
   - **quote / quasiquote reconstruction.** A non-empty quoted list is not
     const-pooled, so it is *rebuilt* at runtime — and the rebuild flattened
     bracket cells to plain pairs. Fixed via **runtime calls (MVP), no new
     opcodes**, and crucially **construction is parallel, never build-then-coerce**:
     - `quote`: `requote` (`helpers.py`) detects a bracket-pair spine and rebuilds
       it with a `(bracket-cons …)` call (preserving the `bracket-nil` terminator),
       recursing so each sublist's style survives.
     - `quasiquote`: `_qq_list` (`quasiquote.py`) takes a `bracket` flag and picks
       its constructor at each emission site — `bracket-cons` (+ `bracket-nil`
       tail) for whole-list and dotted builds, `bracket-nil` for empty, and a
       runtime **`bracket-splice`** (`koskari.types`) for `,@`. No plain list is
       built and converted; the cells are built bracket from the start.
     - **`bracket-splice` marks only the leading cell.** Since repr is governed by
       the head cell, `bracket-splice` flattens its segments like `PAIR_SPLICE`
       but returns `bracket-pair(head, plain-pair-tail)` — one bracket cell, no
       full-spine copy. So a spliced `` `[a ,@xs] `` has a bracket head and a
       plain-pair tail (reprs `[a …]`); a non-spliced `` `[a b] `` is bracket
       throughout (via `bracket-cons`).

     Dedicated bracket build opcodes (`BRACKET_CONS`/`BRACKET_PROPER`/
     `BRACKET_PAIR_SPLICE`) remain a possible later optimization over these
     runtime calls.
3. **Compiler guard flag.** Add a `Compiler`-level flag (accept / warn / error)
   and the single bracket-pair predicate at `compile_pair` entry. Default to
   **accept** so existing behavior is unchanged until opted in.

## Bracket-nil: parallel empty-list singletons

The empty bracket list is a distinct singleton, **`bracket-nil`**, parallel to
`nil`. They are *not* related by inheritance — they are siblings unified only by
a predicate:

```
pair  (KosPairType)
├── nil          : NilType          tp_base = pair          singleton _KosNil
└── bracket-pair : BracketPairType  tp_base = pair
    └── bracket-nil : BracketNilType tp_base = bracket-pair  singleton _KosBracketNil
```

- **No multiple inheritance.** A static C `PyTypeObject` has a single `tp_base`.
  Making `bracket-nil` inherit *both* `bracket-pair` and `NilType` would require
  a runtime `tp_bases` tuple and is fragile (slot-resolution across two types
  that each override `tp_new`/`tp_dealloc`/repr). It also buys nothing: nil-ness
  is recognized by an **identity** test, not by `isinstance`. So they are
  deliberately parallel siblings. `bracket-nil` is-a `bracket-pair` (and thus a
  `pair`); it is **not** a `NilType`. `nil` is **not** a `bracket-pair`.

- **`KosNilP` is the unifier.** `KosNilP(v)` (pair.h) is true when `v` is either
  empty-list singleton: `KosNil_Check(v) || KosBracketNil_Check(v)`. This is the
  list-terminator / empty-list test and **replaces `KosNil_Check` everywhere
  "end of list" is meant** — the spine-walk macro `KosPair_IsCell`, the repr
  family, `length`/`copy`/`is-proper`/`eq`, `CAR`/`CDR`/`PAIR_SPLICE` opcodes,
  and the matcher. `KosNil_Check` (single-singleton) now survives only where
  *specifically the canonical nil* is meant (currently: the singleton's own
  allocation machinery). This is a second audit, parallel to the `CheckExact`
  one. It is exposed to Python as `koskari.lib.nilp` and backs `nil?`
  (`koskari.types.is_nil`).

- **`nil?` covers both.** `nil?` is now a runtime predicate over `KosNilP`, not a
  compile-time identity inline. The old `compile/nil?` syntax (which expanded to
  `(is value nil)` — single singleton) was removed; re-inlining as an opcode
  (`NILP` / `CHECK_NIL`) that checks both singletons is a possible future
  optimization.

- **Equality: empties compare equal, distinct identity.** `() == []` is true and
  `(equal? [1 2] (1 2))` holds — bracket vs paren is style, so the two
  terminators compare equal as list ends (`pair_eq`: both-empty → equal,
  one-empty → unequal). But `'[] is '()` is false — they are distinct objects.
  (nil/bracket-nil are unhashable, like all pairs, so equal-but-distinct raises
  no hash-invariant concern.)

- **Rendering, in both flavors.** Each empty renders as its own *form*:
  `kos_repr`/`str` give `()` for `nil` and `[]` for `bracket-nil` (this changed
  `nil` from the old `"nil"` token). The Python debug `repr()` keeps *named*
  terminators — `nil` and `bracket-nil` — so `cons(1, 2, nil)` /
  `bracket-cons(1, 2, bracket-nil)` stay readable.

- **Const recognition.** `bracket-nil`'s type is registered alongside
  `type(nil)` in the compiler's `CONST_TYPES` and helpers' `_PYCONS_DIRECT`, so
  it is treated as a self-quoting literal exactly like `nil`.

## Future / later phases (out of scope for v1)

- **Advisory matcher style hint (the symmetric half).** A syntax's matcher
  pattern is the single source of truth for "what role does this position play"
  — which is already why hygiene rides the matcher (`HygienicMatchCompiler`,
  binders, scope regions in `koskari/matcher.py`). The matcher's
  parallel-compile-time-metadata channel can carry a per-position
  `style: bracket-recommended` hint on binding-group pattern nodes
  (`binding_inner_pattern`, `bindings_grouped_list_cell_pattern`). **Invariant:
  this is advisory only.** It must never affect the native matcher's
  accept/reject; `(let ((a 1)) …)` keeps matching and compiling exactly as
  today. The hint is consumed by a separate diagnostics pass (the style lexer /
  LSP), surfaced as a fix-it (`( )` → `[ ]` here). Every macro author then gets
  per-position style guidance for free without the linter hard-coding `let`,
  `cond`, `letrec`, etc.
  Ref: [api/matcher-hygiene-extensions.md](api/matcher-hygiene-extensions.md),
  [MATCHER.md](MATCHER.md).
- **Repr fidelity beyond leading-cell.** Any richer round-tripping of mixed
  delimiters is deferred; v1 commits only to the leading-cell rule.
- **Pragma gating.** Replace the simple compiler flag with proper pragma
  control once `koskari/compile/pragma/` supports it.

## The two-sided model (summary)

| Direction | Site | Catches | Severity |
|---|---|---|---|
| Bracket leaked into eval position | `compile_pair` entry | `(cons 0 [a 1])` | accept / warn / error |
| Paren where a pattern recommends bracket | matcher node metadata | `(let ((a 1)) …)` | advisory suggestion only (future) |

Both are "single source of truth" sites: `compile_pair` is the sole place a
pair becomes a call; a syntax's matcher pattern is the sole place a position's
data-vs-call role is declared.

## Verification

- `make test` green across phases — in particular `phase_lib` (the new type and
  its repr), `phase_infra` (reader dispatch), and `phase_compile` /
  `phase_specials` (the `compile_pair` guard in all three modes, and that `let`
  bindings never trip it).
- A round-trip test: `[ ]` source reprs back with `[ ]`; `'[]` and `'()` both
  print as the empty list.
- Guard-mode tests: `(cons 0 [a 1])` accepts / warns / errors per flag;
  `(cons 0 '[a 1])` never trips; `[if …]` trips (above the special split).
- `make test-native` / `make test-asan` for the C type (free-list path, GC
  traverse/clear, no leak on the new allocation route).

## Naming

`bracket-pair` was chosen deliberately over `data-pair` / `form-pair`: it states
exactly what it is (a pair that came from brackets), and all downstream behavior
keys off the plain predicate "is this a bracket-pair." `form-pair` was rejected
as backwards — in the project's own vocabulary `( )` marks call/syntax *forms*,
so naming the data delimiter "form" points the word at the half it least
describes.

## The end.
