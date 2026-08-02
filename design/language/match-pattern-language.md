# Match pattern language

## Status

Design settled; partially built. The runtime engine, the `MATCH_CASE` /
`MATCH_STORE_NAMES` opcodes, and the `KOS_CACHE_MATCHER` cache are landed (see
[match-case-form.md](match-case-form.md)). The **surface language** described
here — the delimiter clause shape, the head-driven pattern classifier, the
reader-sugar head vocabulary, and guards — supersedes the interim imply-`FORM`
`compile/match` currently in the tree and is the target we build to.

## Purpose and long-term vision

`match` is meant to be a **low-level, universal shape-detection primitive** for
koskari as a whole — not just a user-facing `match/case` statement. Nearly every
place the compiler or runtime hand-walks a spine, counts a list, checks a head,
or loops to classify a form is a place a compiled matcher can do the same work
declaratively, faster, and with binding built in. The goal is to grow this into
the shared substrate that **replaces most ad hoc shape detection** across the
tree, the way the matcher already backs macro dispatch (`syntax-rules` /
`syntax-match`). This document specifies the runtime `match/case` surface; the
engine underneath is the same one the compile-time matcher uses, via a different
subject adapter.

`match` is a **sequence-unpacking** form at heart, but the pattern language is
fully expressive: type/class checks, predicates, computed extraction, mappings,
alternation, and guards, all classified uniformly by head.

## Design invariants

These are load-bearing and non-negotiable:

- **The special makes no shape decisions.** In `compile/match` the only
  structural checks are `pair` and `nil` (walking clauses and bodies) plus the
  `else:` marker. All pattern classification is delegated to the matcher's own
  parser.
- **Brackets are visual aids, never signals.** `()` and `[]` are value-equal and
  compile identically; the special must never branch on the cell being a bracket
  type. A pattern's *kind* is decided by its **head** (or by a marker like `::`),
  which is a distinct value regardless of how it was bracketed.
- **Reader sugars mint distinguishing heads.** Where Python encodes pattern kind
  in bracket *type* (`[…]` sequence, `{…}` mapping, `Name(…)` class), koskari's
  `#`-reader forms and subscript produce distinct *head symbols* (`#tuple`,
  `#list`, `#dict`, `item`). Classifying on those heads recovers Python's
  distinctions without ever touching cell type.
- **Externs resolve at match time, and are requested.** Type / class / predicate
  names in a pattern become matcher externs, loaded from the namespace at match
  time; `compile/match` calls `request_var` on each so a name bound in an
  enclosing scope is captured into the closure.

## The clause: a mandatory delimiter

Each arm is `[[PATTERN] BODY...]` — the pattern is wrapped in its own delimiter,
separate from the body. The delimiter is a **pure container**, exactly like the
condition bracket in `compile/switch` (`[[cond…] body]`); the special reads it
with `pattern, body = clause` and never inspects the bracket kind.

```scheme
(compile/match SUBJECT
  [[PATTERN] BODY...]
  [[PATTERN] BODY...]
  [else: BODY...])
```

The delimiter is what keeps a multi-token pattern (`x :: int`) from flattening
into the body once the clause is a flat s-expr. It is **dual-mode**: the layout
produces it by wrapping the case header, and the s-expr author writes it
directly — so the s-expr form stands alone, independent of the layout. This is
why a "raw layout that separates header from body" is not sufficient: it would
only fix the layout path.

The `block:` form already separates a case header from its body
(`(block: (case PATTERN) (body...))`), and subscript / `#`-forms fuse in the
header, so a single-form pattern arrives clean; the delimiter's specific job is
to preserve that grouping through the flat clause on the s-expr side.

## Head vocabulary

Every pattern is an atom or a headed form; the classifier dispatches on head or
marker. Reader sugar (right column) is how you write it; the head it produces is
what the matcher keys on.

| Kind | Surface (sugar) | Head / marker | Lowers to |
|---|---|---|---|
| capture | `x` | atom (symbol) | `CAPTURE x` — bind this position |
| literal | `5` `"s"` `foo:` `'sym` | atom | `LITERAL` — must equal |
| typed capture | `x :: T` | `::` marker | `CAPTURE(x, TYPE T)`, `T` an extern |
| type check | `:: T` | `::` marker | `TYPE T`, no bind |
| sequence | `#[a b *: r]` | `#list` | `FORM(MATCH …)`, `*:` → `REST` |
| sequence (tuple) | `#(a b *: r)` | `#tuple` | `FORM(MATCH …)` |
| mapping | `#{k: v …}` | `#dict` | `MAP` (new engine node) |
| class | `Point[x y]` | `item` | `CLASS Point x y` |
| class (kw field) | `Point[x: 0 y]` | `item` | `CLASS`, field `x`==0, bind `y` |
| predicate | `(? pred)` | `?` | `PREDICATE` test |
| extraction | `(app f p)` / `(= f p)` | `app` / `=` | apply `f`, match result |
| alternation | `(or a b)` | `or` | `ONEOF` |
| negation | `(not p)` | `not` | `NOT` |

`::` and the subscript/`#`-heads are the common ones; the combinators (`or`,
`not`, `?`, `app`) are the Racket-style power set (see Prior art). A `::`
constraint may itself carry a sub-pattern (`(x :: SUBPAT)`), which is the
as-binding / Racket `and` mechanism — capture the whole *and* match `SUBPAT`.

### Sequences and `*:`

A sequence is always the explicit `#`-form (`#[…]` or `#(…)`), never bare
juxtaposition — so nothing is an accidental sequence. `*: name` inside a spine
composite is a rest capture; this rule lives in the **matcher surface** itself
(`form_factory` / `match_factory` recognize `*:` → `REST`), so it is available to
every matcher user, not just `match`. The matcher-native `(REST slot)` remains.

### Mapping keys are externs

`#{}` evaluates its keys, which is the right model for patterns: a key is a
literal (`#{"kind": pat}`) or an in-scope/computed value (`#{k: pat}`), resolved
at match time through the **same extern channel** as `::` type names. That is
strictly more than Python's constant-only keys, and it falls out of `#{}`'s
semantics rather than being bolted on. (Contrast `(#dict k: v)`, whose keyword
makes a *string* key `"k"` — the wrong model for pattern keys.) A mapping pattern
requires a new engine node (`MAP`), which does not exist yet.

### Class is Python semantics

`Point[x y]` → `(item Point x y)`; `item` is reserved in pattern position and
lowers to `CLASS`. The engine's `CLASS` already does the Python thing —
isinstance plus `__match_args__` for positional fields and attribute access for
keyword fields — so it works on arbitrary classes, not just declared structs
(the way Racket's `struct` patterns require). Positional fields need
`__match_args__` on the class (dataclasses/namedtuples have it for free);
keyword fields (`Point[x: 0 y]`) work on any class.

### Contextual reservation

`item`, `#list`, `#tuple`, `#dict` mean their builtins in **expression** position
and their pattern specifiers in **pattern** position. This contextual
reservation is the same move class-via-subscript already needs, and it is clean
because patterns are a distinct parsing context.

## Guards: the arm as a mini-program

An arm is a small pipeline with a uniform *advance* edge — any stage that can
fail jumps to the next arm with the subject left in place:

```text
TEST   (MATCH_CASE)         matched? → BIND, else → ADVANCE
BIND   (MATCH_STORE_NAMES)  land the captures
GUARD  (when / unless)      predicate holds? → COMMIT, else → ADVANCE
COMMIT (body)               run it; the subject is consumed here
```

Surface:

```scheme
case PATTERN when COND:
  BODY
case PATTERN unless COND:
  BODY
```

`when` / `unless` are reserved words that split the header: pattern, then the
guard form, then the body. Guards run *after* binding (they reference the bound
names) and captures persist even when a guard fails — Python-consistent.

The opcode support is a **`keep`-subject flag on `MATCH_CASE`** (its free 4th
`EXTRA` slot). Normally an arm commits at the TEST stage — `MATCH_CASE` pops the
subject on match. A guarded arm sets `keep`, so `MATCH_CASE` leaves the subject
on the stack; the guard then does `JUMP_FALSE next_arm` (advance, subject intact)
or falls through, `POP`s the subject, and runs the body. The `keep` flag is the
one primitive that decouples "this pattern matched" from "this arm commits," and
it is what later absorbs `as`-binding, `or`-patterns, and multiple guards as
further stages.

## Lowering

Per arm, `compile/match`:

1. `pattern, body = clause` (pair/nil), unwrap the delimiter.
2. Lower `PATTERN` through the head-driven matcher parser to a frozen 6-tuple
   const.
3. `request_var` each extern in the frozen matcher; declare each capture name.
4. Emit `MATCH_CASE` (with `keep` set iff the arm is guarded), then
   `MATCH_STORE_NAMES` (or `POP` for a capture-less arm), the guard block if
   present, the body, and the `JUMP`/`LABEL` chain.

The subject is evaluated once and held on the stack across the arm chain, exactly
as the discriminant in `compile/switch`. The opcode / cache mechanics are in
[match-case-form.md](match-case-form.md).

## Simple vs. powerful (rule/case, deferred)

koskari already splits pattern surfaces into a simple tier (`syntax-rules`,
RULE dialect) and a powerful tier (`syntax-match`, MATCH dialect) over one
engine. A runtime `rule/case` — a simple, implicit-sequence form with no heads,
paralleling `syntax-rules` — is a natural sibling, but is **deferred**: the RULE
parser is heavily tailored to syntax forms (it wants a declared literal set) and
reserves `...` for syntax, while runtime patterns use `*:`. We first try to make
`match/case` friendly enough for the easy cases on its own; if the `#` on
sequences proves to be a real papercut in practice, `rule/case` returns. The
division, if built, is principled: `rule/case` makes exactly one assumption
(it's a sequence), `match/case` makes none (every kind says its head).

## Prior art

The s-expr matching canon informs this design:

- **Racket `match`** — the reference; `define-match-expander` (user-extensible
  patterns) is the model for a future `define-matcher`. Its combinators
  (`and`/`or`/`not`/`?`/`app`) are the power vocabulary borrowed here.
- **Wright's Scheme `match`** — origin of `(? pred)`, `(= extractor pat)`, and the
  `(=> fail)` failure continuation (an alternative to `when` guards that hands the
  body a first-class *advance*).
- **Clojure `core.match`** — `:when`, `:as`, map patterns, and Maranget
  decision-tree compilation (a reference if cross-arm test sharing is ever
  wanted).
- Non-s-expr semantic references: OCaml / Rust / Erlang for guards,
  exhaustiveness, `as`, and or-patterns.

The engine already has `ONEOF`, `NOT`, `PREDICATE`, `CLASS`, and
capture-with-subpattern (`::`) — most of Racket's *semantic* core. So much of the
remaining work is surface spelling plus a few additions (`?`, `app`/`=`, the
`MAP` node) rather than new matching machinery.

## Open items

- **`MAP` engine node** for mapping patterns — the one genuinely new primitive.
- **`?` / `app` / `=` heads** — small additions; `PREDICATE` already exists.
- **Guard `keep` flag** on `MATCH_CASE` and `when`/`unless` in special + layout.
- **Delimiter clause + head-driven classifier** — supersedes the current
  imply-`FORM` `compile/match`; the layout wraps the header into the delimiter.
- **Single-element delimiter content**: a lone atom is capture/literal, a lone
  headed form is that construct; sequences are always the explicit `#`-form, so
  bare juxtaposition inside the delimiter is never a silent sequence.
- **`rule/case`** simple tier — deferred (above).

Parameterized matcher templates (`define-matcher`) are a **separate
effort** with their own design: [define-matcher.md](define-matcher.md).

## Related

- [match-case-form.md](match-case-form.md) — opcodes, `KOS_CACHE_MATCHER`,
  lowering, stack contract.
- [matcher-ng-rollup.md](matcher-ng-rollup.md) — the unified matcher engine;
  Phase 8 is runtime match/case.
- [../LAYOUTS.md](../LAYOUTS.md) — layout conventions.
