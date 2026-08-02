# define-matcher

## Status

Design in progress. The shape below is the working model from design
discussion; the **type and value association semantics are deliberately
unsettled** (see Open questions) and warrant deliberation before any
implementation. This is its own effort, on its own schedule — not a
follow-up item of the runtime match/case work, though it builds on the
same matcher engine and pattern dialects.

## Purpose

`define-matcher` names a **parameterized matcher construction**: a way to
build complex, detailed matcher shapes out of the native units (`FORM`,
`ONEOF`, `CLASS`, `MAP`, `APP`, captures, typed captures, ...) — the same
role the Python-authored composites (`BINDING`, `BINDINGS`, `FORMALS`,
`BODY`) play today, opened to koskari source. Parameters are the point:
without them the form is mere aliasing; with them it defines reusable
shapes with caller-chosen types, values, sub-patterns, and binding names.

## Working model

### A defined matcher is a template, not a pattern

`define-matcher` binds a **matcher template**: a formals list plus an
unlowered pattern body (source form). Nothing lowers at definition time.

```scheme
(define-matcher pair-of [T]
  #[(a :: T) (b :: T)])

(define-matcher tagged [tag inner]
  #[(val: tag) inner])
```

### Expansion: substitute, then classify

At a use site `(pair-of int)`, resolution finds the template, substitutes
each formal symbol with its argument *form* throughout the body, and hands
the substituted body to the normal pattern classifier, which lowers it to
primitives inlined into the use site's frozen tuple. The template is a
compile-time expander — a data-defined instance of the registry's existing
`factory(head, raw_args)` protocol. There is **no `CALL_MATCHER`** node
(rollup resolved decision 3): frozen tuples stay pure primitives, and a
matcher frozen before a rebind keeps its tree.

Because substitution is source-level, a formal can stand in any symbol
position:

- **type / extern position** — `(a :: T)`, `T` ↦ `int`
- **nested pattern position** — `inner` ↦ `#[x y]`, `Point[x y]`, or
  another template invocation
- **slot-name position** — the capture name itself is a formal, so callers
  choose binding names; this is `slot_prefix` / `BINDING_CELL` as surface
  syntax, and the answer to slot collisions (two uses of the same template
  in one arm collide on fixed slot names — a correct freeze-time error —
  so a repeat-use matcher parameterizes its slots)
- **literal / value position** — `(val: tag)`, `tag` ↦ `"job"`

### Module-scoped resolution

A definition is a **binding**, never a global-registry mutation (the
assert-gate lesson: a setting's effect derives from the value, not the act
of applying it). `define-matcher` lowers to a quoted runtime build call
(`build-matcher …`, in the spirit of `build-syntax`) evaluated at module
load, binding the template to **`matcher/NAME`**. Resolution at a pattern
head follows the layout / syntax convention: builtin primitive first, then
`matcher/NAME`, then bare `NAME` in the **caller's namespace**, type-gated
to the template type. Imports and `.kio` round-trips are inherited from
the syntax story — the template is an ordinary module binding.

The plumbing cost: `parse_matcher` / `parse_case_element` grow an optional
namespace argument threaded from the compiling special; callers without a
namespace get the builtin vocabulary only.

### Use-site spelling

Invocation is always the **headed form** — `(NAME args…)`, zero-arg as
`(NAME)`. A bare symbol in pattern position stays a capture,
unconditionally; a bound matcher name never silently shadows captures.

### v1 non-goals

- **Hygiene.** Substitution is a plain template splice; a formal named the
  same as a body capture is on the author. syntax-rules-grade hygiene can
  layer later (the `Hygiene` split gives it a home).
- **Option keywords.** Positional formals only; `scope:` / `mode:` style
  options are a later rev.

## Open questions

### Type and value associations (the one to dwell on)

How a formal *associates* with what it stands for is undecided. Plain
source substitution makes every formal position-agnostic — `T` becomes a
type extern only because it landed in `:: T` position, `tag` a literal
only because it landed in `val:` position. That is maximally flexible and
minimally declared. The alternatives worth weighing before committing:

- **Declared formal kinds** — the formals list annotates intent
  (`[T :: type]`, `[tag :: value]`, `[inner :: pattern]`), letting the
  definition validate its arguments, produce real arity/kind errors at the
  use site, and document itself. Costs a small grammar for formals.
- **Association by position only** (the working model) — no declarations;
  errors surface later, from the classifier over the substituted body.
- **Value capture vs. extern reference** — when an argument is a value
  (`(tagged "job" p)`), should it embed as a const in the frozen tuple, or
  be routed as an extern resolved at match time? Same question for types:
  `(pair-of int)` currently implies extern `int`, but a template might
  want to *close over* a resolved value at definition or expansion time
  instead. Where a template argument lands — const pool vs extern table —
  changes drift behavior, closure capture, and what `request_var` must do.

### Others

- Precedence details: may `matcher/NAME` shadow non-primitive builtins
  (e.g. a module-local `BINDING`)? Primitives themselves unshadowable.
- Which dialect the definition body is written in (case dialect vs matcher
  surface), or whether the template records its dialect.
- Whether templates may invoke other templates recursively, and the cycle
  policy (the engine forbids recursive matchers; template expansion must
  terminate).

## Related

- [matcher-ng-rollup.md](matcher-ng-rollup.md) — the engine, the registry
  factory protocol, resolved decision 3 (composites always inline).
- [match-pattern-language.md](match-pattern-language.md) — the case
  dialect a template body would likely be written in.
- `koskari/matcher.py` — the Python composites this generalizes
  (`BINDING`, `FORMALS`, `slot_prefix`, `BINDING_CELL`) and
  `MATCHER_REGISTRY` / `register_matcher`.
- `koskari/compile/specials/syntax.py`, `koskari/syntax.py` — the
  `define-syntax` → `build-syntax` architecture the definition form
  mirrors.
