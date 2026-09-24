# Koskari matcher reference

Canonical reference for the native matcher (`koskari.lib.Matcher`), the Python
pattern builders in `koskari.matcher`, the capture wrappers (`Datum`,
`DatumFrame`), the scoped symbols (`koskari.lib.gensym`) that carry macro
hygiene, and the **runtime match/case system** (the case-pattern dialect, the
`MATCH_CASE` opcode, and `compile/match` / `layout/match`).

**Implementation:** `koskari/matcher.py`, `koskari/hygiene.py`,
`koskari/syntax.py`, `koskari/lib/matcher.{c,h}`;
runtime match/case: `koskari/compile/specials/conditionals.py`,
`koskari/reader/layouts/conditionals.py`,
`koskari/lib/opcode/MATCH_CASE.c`

**Related design (deeper or historical):**

- [api/pattern-matching.md](api/pattern-matching.md) — tagged-tuple IR and migration notes
- [archive/matcher-hygiene-extensions.md](archive/matcher-hygiene-extensions.md) — the superseded discovery-by-pattern hygiene design
- [archive/matcher-ng-rollup.md](archive/matcher-ng-rollup.md) — rollout notes and as-built sketches from before hygiene by marks; the hygiene names it uses are gone
- [archive/macro-hygiene-syntax-boxes.md](archive/macro-hygiene-syntax-boxes.md) — `Datum` in macro expansion
- [language/unified-matcher-transformer-model.md](language/unified-matcher-transformer-model.md) — specials, syntax-rules, and shared finalize pipeline
- [archive/match-pattern-language.md](archive/match-pattern-language.md) — runtime pattern-language design record
- [archive/match-case-form.md](archive/match-case-form.md) — MATCH_CASE opcode / cache design record
- [language/define-matcher.md](language/define-matcher.md) — parameterized matcher templates (open design)

---

## Overview

The matcher is a small pattern language over **spine subjects**: proper lists
of koskari source cells (usually `KosPair`) at compile time, or **runtime
sequences** (tuple/list via a `PySequence_Fast` adapter) at run time. The same
engine serves both: macro / special-form dispatch matches source forms, and the
runtime `match`/`case` statement matches values (each arm's frozen matcher
runs against the subject wrapped as a length-1 sequence). Patterns are built
in Python (`MATCH(...)`, `BINDINGS(...)`, …) or parsed from surface syntax,
lowered to a frozen **6-tuple** IR, and executed by the C `Matcher`.

One compilation path: `MatchCompiler` lowers a pattern to the frozen tuple.

```python
from koskari.matcher import MATCH, DEFINE, BODY, MatchCompiler, make_matcher

matcher = MatchCompiler.compile(MATCH(DEFINE("name"), BODY("body")))
```

`make_matcher(pattern)` and `Pattern.build()` are the same call. A pattern
carries no hygiene metadata: which names a form binds is the handler's
business, and hygiene is provenance on the atoms themselves (see
[Hygiene model](#hygiene-model)).

Special forms and syntax macros attach patterns via `@special(..., patterns=...)`
or `Syntax(...)`. Each arm stores its `matcher` on its `Transform`, built in
`koskari.syntax.Transform.__init__`.

---

## Frozen matcher IR

Lowering emits:

```text
("MATCHER", slot_names, extern_names, consts, nodes, entry_index)
```

- **Matching** and **thaw** use only this tuple.
- `thaw(frozen)` and `Matcher.from_tuple(frozen)` reconstruct a native matcher.

`MatchResult.captures` is a **sparse** dict: only slots that actually bound appear.
Check membership before use.

---

## Datum and DatumFrame

### Datum

`koskari.lib.Datum` is the box the matcher puts input material in: a native
wrapper around a captured value with its source position.

Captures are boxed only on a walk over source, a `KosPair` spine whose cell
positions travel with the value. A tuple, list, or single-value subject yields
raw values in `captures`, including REST tails, multi-cell slices, and
REPEAT rows. The pattern parsers (`parse_matcher`, `parse_case_element`,
`parse_case_pattern`) hand their source element in as a one-cell pair
spine for this reason.

| Field / property | Role |
|------------------|------|
| `.datum` | Wrapped value (symbol, pair spine, list, etc.) |
| `.position` | Source position when matched from `KosPair` cells; else `None` |

**Macro expansion:** substituted pattern-variable values are spliced into the
expansion as their `Datum`. A `Datum` is never marked and never entered:
whatever is inside came from the use site. Finalize (`peel_datums`) unboxes
the cells for compilation and moves each position onto its cell.

The matcher reads through `Datum` layers when it matches shape, so a use
site's boxed atoms match `SYMBOL`, `LITERAL_KW`, and the composites like
bare atoms. A gensym is a symbol and needs no peel: `SYMBOL` takes it,
`TEST_LITERAL` matches it against the symbol it spells by pointer, and a
capture hands it back as itself.

### gensym

`koskari.lib.gensym` is a static subtype of `symbol`: a symbol a template
introduced, scoped by the marks of the expansions that introduced it. It
is interned in the symbol table under its derived spelling, so
`symbol("t#1")` is the gensym when one is alive, and equality is identity
as for every atom. `gensym(name, predicate)` mints a fresh mark from the
process counter; `gensym(base, marks=...)` spells a base under given marks
and raises `ValueError` when another atom holds the spelling.
`gensym.next_mark()` hands out the next mark. Keywords are never scoped.

| Field / property | Role |
|------------------|------|
| `.base` | The symbol the template wrote; `.base` on a plain symbol or keyword is itself |
| `.marks` | Tuple of expansion marks, outermost first |
| `str()` | The derived spelling, `name#hex[#hex...]`, that the compiler declares and resolves |
| `kos_repr` | The base, so printed expansions read as source |

### DatumFrame

`DatumFrame` is a **dict subclass** used when the matcher’s subject is backed by
`KosPair` and a capture needs both nested slot keys and whole-cell provenance.

| Situation | Capture shape |
|-----------|----------------|
| Tuple/list runtime subject | Plain `dict` in `captures[slot]`; values are raw, never `Datum` |
| KosPair root match | Entire `captures` may be a `DatumFrame` |
| Named `GROUP` on KosPair | `captures[slot]` is a `DatumFrame` |
| `repeat_capture` rows on KosPair | Each list element may be a `DatumFrame` |

**DatumFrame fields (conceptual):**

- Behaves like a `dict` of inner capture keys (`name`, `expr`, `positionals`, …).
- `.source` — the **whole matched cell** as a pair spine (not a dict key).
- `.position` — position of that cell.

Use `.source` when compile code needs the raw declaration, binding cell, or
formals list spine without re-parsing from inner keys.

**Not everything is a DatumFrame:** `BINDINGS` stores a bare Python `list` of
per-row dicts (or `nil` for the empty arm). `repeat_capture` list captures are
plain lists unless the engine produced `DatumFrame` per row on KosPair subjects.

---

## Building and invoking matchers

### Python builders

Author patterns with constructors in `koskari.matcher` (see tables below).
Composites (`DEFINE`, `BINDINGS`, …) **expand** to primitive trees in `_expand()`;
they add no match-time Python logic.

### Surface syntax (`parse_matcher`)

`parse_matcher(elem)` lowers **one** meta-syntax element (used inside
`(MATCH ...)` heads in special patterns and in nested constraints):

| Surface form | Lowers to |
|--------------|-----------|
| bare symbol `slot` | `CAPTURE("slot")` |
| atom (non-pair) | `LITERAL(atom)` |
| `(quote sym)` | `LITERAL_SYM("sym")` |
| `(HEAD . args)` with `HEAD` in `MATCHER_REGISTRY` | registry factory |
| `(name . args)` otherwise | `CAPTURE("name", constraints...)` |
| `(:: pat)` / `(type: pat)` | `TYPE` or nested pattern |
| `(val: x)` | `LITERAL(x)` |
| `(CLASS Sym (FIELD ...))` | `CLASS` + `FIELD` nodes |

A named capture takes no options; a keyword other than the constraint
markers in its argument list is an error.

### Rule-level patterns (`parse_explicit_rule_pattern`)

Used for **syntax-rules / syntax-match** rule patterns (`koskari/syntax.py`):

- Walks a **proper list** spine; each cell is lowered with `parse_explicit_rule_pattern`.
- `(HEAD slot mode: ...)` with `HEAD` in `MATCHER_REGISTRY` and symbol `slot` → composite declaration.
- Otherwise → `MATCH(cell1, cell2, ...)`.

Literals in rule patterns become `LITERAL` nodes, not captures.

### Registry heads

Built-ins registered in `MATCHER_REGISTRY` include:

`FORM`, `MATCH`, `GROUP`, `ONEOF`, `LIST`, `NOT`, `REPEAT`, `OPTIONAL`, `REST`,
`TAIL`, `CAPTURE`, `BIND`, `VALUE`, `KEYWORD`, `SYMBOL`, `PAIR`, `BODY`,
`DEFINE`, `DECL`, `BINDING`, `BINDINGS`, `FORMALS`, `USING`, `LITERAL`,
`LITERAL_KW`, `LITERAL_SYM`, `TYPE`, `PREDICATE`, `TEST_ANY`, `FIELD`, `APP`.

---

## Structural primitives

These lower to native nodes. None register hygiene unless wrapped by a
hygienic composite or by `VALUE` / `BODY`.

| Pattern | Match behaviour | Capture |
|---------|-----------------|---------|
| `TEST_ANY()` | Any one spine element | No |
| `SAME(slot, depth=0)` | One element equal to prior `slot` bind in current capture dict (`depth` 1 = parent `GROUP` frame; `depth` > 1 fails). `DatumFrame` operands are unsupported (match fails). Duplicate `BIND`/`CAPTURE` for the same slot is a freeze error — use `SAME`. RULE patterns lower repeats to `SAME`. | No |
| `LITERAL(v)` | Equal to `v` | No |
| `TYPE(extern)` | Extern predicate (`symbol`, `pair`, `keyword`, …) | No |
| `PREDICATE(name)` | Named extern predicate | No |
| `LITERAL_KW(s)` | Keyword `keyword(s)` | No |
| `LITERAL_SYM(s)` | Symbol `symbol(s)` | No |
| `MATCH(p1, p2, …)` | Sequence on current spine | Inner `CAPTURE` / `GROUP` slots |
| `FORM(inner)` | One element; match `inner` on its contents. Runtime descent is PEP 634-gated: KosPair / list / tuple / `collections.abc.Sequence` only — `str`/`bytes`/`bytearray` are atoms, mappings / sets / iterators soft no-match (an iterator is never consumed by a failed arm) | Via inner |
| `GROUP(slot, …)` | Nested dict frame | `captures[slot]` → dict or `DatumFrame`; anonymous `GROUP(None, …)` is scratch only |
| `ONEOF(b1, b2, …)` | First succeeding branch | Per-branch capture scopes |
| `REPEAT(sub, min_count=0)` | Greedy repeat | List via wrapping `CAPTURE` |
| `OPTIONAL(sub)` | Zero or one | Via inner |
| `REST(slot=None, min_tail=0)` | Consume tail | `CAPTURE(slot, REST(...))` when named |
| `NOT(sub)` | Fails if `sub` matches current cell | No |
| `LIST(p1, …, key=slot)` | One list element matching `FORM(MATCH(...))` | Optional `CAPTURE(slot, …)` |
| `CLASS(name, FIELD…)` | Extern-typed object with fields | Via `FIELD` captures |
| `FIELD(pat)` / `FIELD(name, pat)` | One field | Inner slot |
| `APP(fn, sub=None)` | Call extern `fn` on the element (demand path), match `sub` against the result in a projected single-element subject; a raising extractor propagates, a genuine async suspension raises `KoskariSyncViolation` | Via `sub` |
| `MAP(MAP_ENTRY…)` | One mapping element (dict or `collections.abc.Mapping`); each entry's key must be present with value matching the entry's sub-pattern; extra keys ignored (PEP 634) | Via entry sub-patterns |
| `MAP_ENTRY(key, sub=None, extern=False)` | One MAP entry; key is a const, or an extern name resolved at match time when `extern` | Via `sub` |

**Callable externs run on the demand path** (`Koskari_DemandSync`): `PREDICATE`
and `APP` externs may be plain koskari callables with no wrapping; a plain
Python awaitable result is eagerly stepped, and a genuine async suspension
raises `KoskariSyncViolation`.

### Ergonomic shorthands (primitives)

| Shorthand | Expands to |
|-----------|------------|
| `BIND(slot)` | `CAPTURE(slot)` |
| `SYMBOL(slot)` | `CAPTURE(slot, TYPE("symbol"))` |
| `KEYWORD(slot)` | `CAPTURE(slot, TYPE("keyword"))` |
| `PAIR(slot)` | `CAPTURE(slot, TYPE("pair"))` |
| `VALUE(slot)` | `CAPTURE(slot)`; the slot holds an expression the form evaluates |
| `BODY(slot)` | `REST(slot, min_tail=1)`; the slot holds a body tail |

### `repeat_capture` and binding helpers

```python
repeat_capture("rows", FORM(VALUE("x")))
```

Lowers to `CAPTURE(slot, REPEAT(GROUP(None, inner)))`. Each iteration produces
one row (usually a `dict` or `DatumFrame`).

| Helper | Purpose |
|--------|---------|
| `binding_inner_pattern()` | `(name expr)` or `(expr as: name)` inside `FORM` |
| `binding_group_capture_pattern()` | Same as single `BINDING` row |
| `bindings_grouped_list_cell_pattern()` | `((name expr))` grouped let cell |
| `formals_inner_pattern()` | Full proper-list formals decomposition |

---

## Hygiene model

Hygiene is provenance, not discovery. Nothing in a pattern says what a form
binds; the special that compiles the form knows, and the compiler resolves
names with that knowledge at the point it declares or references them.

### Marking

Each arm expansion draws a fresh mark from `gensym.next_mark()`, a counter
in C seeded once per process from a random 32-bit base, so a scoped
spelling serialized into a `.kio` by one process is not minted again for a
different name by the process that loads it.

- A template arm scopes every template-literal symbol while it transcribes
  (`expand_template_with_hygiene(template, bindings, mark)`). Keywords pass
  through unchanged. Substituted captures are spliced in as their `Datum`,
  unmarked.
- A procedural arm's output is scoped after the handler returns
  (`mark_expansion`): every symbol outside a `Datum`. A `Datum` the
  handler built, or spliced from a capture, is left alone.
- Scoping a gensym extends its tuple. That is the only way marks
  accumulate: a template symbol inside a `define-syntax` the outer template
  introduced is template-literal to the inner expansion as well.
- Finalize peels the `Datum` boxes (`peel_datums`) and hands the form, marks
  intact, back into the compile loop.

A keyword formal a template writes therefore binds under the name the
caller writes, and the template's scoped reference to it falls back to that
name. A keyword-named parameter is the one binder hygiene does not rename.

### Reading a scoped atom

A gensym reaches the compiler wherever the template symbol would have, and
every symbol test accepts it. The rule at each site:

| The atom is read as | Do |
|---------------------|----|
| a binder or a reference | pass the atom; the processor spells it |
| a head in call position | `find_compiled` tries the spelling's candidates, then the bare name |
| a tag, label, literal, attribute, import member, operator | `.base`, or `strip_mark` when the operand may not be an atom |
| quoted data | `quote` / `quasiquote` project to the base; `quote-syntax` keeps the gensym |

`koskari.types.strip_mark` projects one value, `strip_marks` a whole form;
`type(x) is gensym` is the exact-type test, and a known atom reads `.base`.
Handlers that go through `gather_formals`, the unpack helpers,
`compile_symbol`, and the fragment's `request_var` / `declare_var` get
the key for free; a handler that reads a name by hand applies the table.
Keyword-named formals declare the key too, so `argv:` in a template binds
`argv#N` and the body's `argv#N` finds it.

### Resolution

Declares are immediate: a marked binder is declared under its gensym. A
marked reference is emitted as its atom, and the fragment raises its `marked`
flag. When the fragment completes and every declaration visible to it is
final, `Resolver.resolve` in `koskari.hygiene` chooses each name, walking the
instruction stream through `ByteCodeFragment.rewrite_vars`, which tracks the
inline scopes. `function_constant` completes an unmarked function body at
once; a marked one is deferred as a `HygieneFunctionSpec`, which completes
against its ancestry chain when the enclosing fragment completes and is then
replaced by its `FunctionSpec`. A sibling defined later in the same body, as
the functions of a `labels` form are, is therefore visible to a closure
defined before it.

The candidates for `gensym(t, marks=(1, 2))` are `t#1#2`, `t#1`, `t`,
longest first.
The first candidate declared in a visible scope wins: the inline scopes open
around the reference in the instruction stream, the fragment itself, the
fragments enclosing it. Failing that, a marked spelling already bound in the
module wins (a definition an earlier turn made, as a macro-generating
macro's expansion leaves behind). Failing that, the bare name, which the
runtime resolves as usual. A binder in a sibling scope is not visible and
does not count. The chosen atom is written into the instruction operands,
the closure requests, and the inline scopes' namespace specs; the
`ByteCodeProcessor` spells every name when it copies the rows.

Free template references therefore reach the runtime bare, and a function
carries one closure slot per name, not one per expansion.

### Heads

`find_compiled(gensym)` tries `compile/<candidate>` for each candidate, so a
transformer the same expansion defined under a marked spelling is found from
the template that uses it, and every other marked head falls through to the
bare special or macro. Specials and macros win over local bindings in head
position regardless, so marks change which transformer runs only in that
one case.

### Quoting

`quote` and `quasiquote` strip marks from literal data, so `'(t)` in a
template yields the bare symbol. `compile/quote-syntax` and
`compile/quasiquote-syntax`, read as `#'` and `` #` ``, keep them. The
syntax specials quote patterns, literals, and templates with `quote-syntax`,
which is what lets a macro-generating macro's inner template reach the
outer expansion's binder; a procedural macro a template defines builds its
output with `quasiquote-syntax` for the same reason, since a plain backtick
projects the outer marks and the output resolves at the use site. The codex
writes a gensym constant as the `SYMBOL` record of its base: no mark
survives compilation. Sealed code reaches its bindings through spelled
operands, and a template loaded from a `.kio` is source that its next
expansion marks afresh.

### Display

`kos_repr` of a gensym prints the base, so `macroexpand-1` output and the
corpus `expand` records read as source. `str` and `repr` show the derived
spelling.

### Typical special-form shapes

```python
# define: name bound; value is an expression the form evaluates
_MATCH_DEFINE = MATCH(DEFINE("name"), VALUE("value"))

# let: bindings, then the body
_MATCH_LET = MATCH(BINDINGS("bindings"), BODY("body"))

# lambda: formals, then the body
_MATCH_LAMBDA = MATCH(FORMALS("formals"), BODY("body"))
```

`VALUE` and `BODY` lower exactly as `BIND` and `REST(min_tail=1)`; the
spelling records the slot's role for the reader.

---

## Composite reference (structure and compile use)

### DEFINE

- **Shape:** one symbol cell.
- **Expansion:** `CAPTURE(slot, TYPE("symbol"))`.
- **Compile:** function name, macro name, `define` LHS.

### DECL

- **Shapes:** bare symbol; `(name)`; `(type as: name)`; `(name . kw-tail…)`.
- **Tail:** `decl_tail` is captured verbatim; matcher does not parse keyword pairs.
- **Compile:** declarations, typed bindings, attribute tails.

### BINDING / BINDINGS

- **BINDING:** one binding cell `(name init)` or `(init as: name)`.
- **BINDINGS:** one spine cell — either `nil` or a proper list of binding cells.
- **Grouped let:** `[[t 1]]` uses `bindings_grouped_list_cell_pattern()` inside the `FORM` arm.
- **Unpack:** `names, values = matched["bindings"]` in helpers when converting to bytecode.

### FORMALS

- **One** proper-list cell per `FORMALS`; improper formals are rejected.
- Phases (all optional): positionals → defaults → `*:` → kw-only → `**:`.
- A positional or keyword name may carry a leading `&` **by-ref** marker;
  parsing strips it and records the name in the `by_refs` tuple on `Formals`
  (never on `*` / `**`). See [SYNTAX.md](SYNTAX.md) (By-ref formals).
- **Compile:** `gather_formals` in `koskari.compile.helpers`.

### USING / USING_UNPACK

- **USING:** ordered `ONEOF` of unpack, name+expr, as-form, bare expr.
- **USING_UNPACK:** only `((pattern…) expr)`.

---

## Syntax integration

### `Syntax` and `Transform`

Each transform arm:

1. Lowers `pattern` with `MatchCompiler` and stores the `matcher`.
2. `try_expand` runs `matcher.match(source_obj)` then `handle_expand`.

`SyntaxTransform.handle_expand` transcribes the template with a fresh mark;
`MacroTransform.handle_expand` runs the handler and marks its output. Both
end in `koskari.syntax._finalize_macro_output`, which applies the layout
handlers and then `finalize_expansion`.

### Pattern parsing under marks

`parse_syntax_rules_pattern` reads a marked pattern atom through
`strip_mark` to classify it, matches a literal by name, and names a pattern
variable's slot by the atom's key, so a template's marked reference finds
its own variable and a bare use-site symbol does not.

---

## Choosing a matcher (quick guide)

| Intent | Use |
|--------|-----|
| Match and bind any single cell | `BIND` |
| Match a symbol cell | `SYMBOL` |
| Match the symbol a form defines | `DEFINE` |
| Match an expression the form evaluates | `VALUE` |
| Match a body tail | `BODY` |
| Match any tail | `REST` |
| Match binding list for let | `BINDINGS` |
| Match formals list | `FORMALS` |
| Match exact atom | `LITERAL` / `LITERAL_SYM` / `LITERAL_KW` |
| Repeat rule elements | `repeat_capture` + `REPEAT` |

---

## ONEOF and GROUP nesting

- **ONEOF** branches use separate capture scopes; duplicate slot names across
  arms are allowed, and a branch's slots count as formals.
- **GROUP(slot, …)** opens a nested capture scope whose slots do not count as
  formals of the enclosing pattern.

---

## MATCH dialects (three parsers)

| Parser | Input | Typical use |
|--------|-------|-------------|
| `parse_matcher` | One pattern element | Python special `patterns=`, nested constraints |
| `parse_explicit_rule_pattern` | Proper list spine | `syntax-rules` / `syntax-match` rule patterns |
| `parse_case_pattern` / `parse_case_element` | One match/case clause delimiter / one case element | Runtime `match`/`case` patterns (see the runtime section below) |

Surface heads and capture options are shared; rule-level parsing sequences
cells. The case dialect adds the reader-sugar heads and delegates marked /
registry-headed forms back to `parse_matcher`.

---

## Runtime match/case

The runtime `match` statement is the value-matching consumer of the engine:
`compile/match` (s-expr) and `layout/match` (block syntax) lower each arm's
pattern to a frozen matcher constant and emit a `SWITCH_EQ`-style opcode chain.

### The case-pattern dialect

`parse_case_pattern(delimiter_content)` classifies one clause's pattern;
`parse_case_element(elem)` classifies one element. Both are meta-matchers plus
walkers, the same architecture as `parse_matcher` (`CASE_ELEMENT_MATCHER`,
`CASE_DELIM_MATCHER` in `koskari/matcher.py`). Pattern *kind* is decided by
head symbol or marker — **never by bracket or cell type**; the reader sugars
mint the distinguishing heads:

| Surface | Reads as | Lowers to |
|---------|----------|-----------|
| `x` (bare symbol) | atom | `CAPTURE` |
| `5` `"s"` `foo:` `'sym` | atom / `(quote s)` | `LITERAL` |
| `x :: T` / `(x :: T)` | `::` marker | `CAPTURE(x, TYPE(T))`, `T` extern |
| `#[a b *: r]` | `(#list a b *: r)` | `FORM(MATCH(...))`, `*:` → `REST` |
| `#(a b)` | `(#tuple a b)` | `FORM(MATCH(...))` |
| `#{K: PAT ...}` | `(#dict (#tuple K PAT) ...)` | `MAP`; symbol keys are externs, atom keys consts |
| `Point[x y]` | `(item Point x y)` | `CLASS`; Python field rules (`__match_args__` positional, `k: pat` keyword, self-binding bare symbol after keywords). On 3.9 (pre-PEP 634) the engine synthesizes `__match_args__` for dataclasses / namedtuples, gated `PY_MINOR_VERSION < 10` so a 3.10+ `match_args=False` opt-out is never overridden |
| `(app f PAT)` | head `app` | `APP` |
| `(? pred)` | head `?` | `PREDICATE` |
| `(or ...)` / `(not ...)` | heads | `ONEOF` / `NOT` |
| `(REST r)`, `(CLASS ...)`, any registry head | registry passthrough | via `parse_matcher` |
| bare multi-token list `(a b c)` | — | **error**; sequences are always the explicit `#`-form |

The clause shape is `[[PATTERN] BODY...]` — the pattern rides in its own
delimiter list, which holds one pattern form or the multi-token `::` spelling.
The delimiter is a pure container: the special reads it with `pair`/`nil` only.

### Opcodes and lowering

| Piece | Behaviour |
|-------|-----------|
| `MATCH_CASE` | EXTRA row: no-match label, frozen matcher const, externs name tuple, keep-subject flag; one `KOS_CACHE_MATCHER` slot. Subject held on the stack across arms; wrapped as a length-1 sequence per attempt. No match: subject stays, jump. Match: the engine fills a slot array aligned to the matcher's slot table (`KosMatcher_RunSlotsVectorcall`), the opcode binds each slot that fired into the namespace and pushes nothing (subject consumed unless `keep`). The subject is a single runtime value, never a source spine, so captures are raw. **Unfired slots are skipped** -- an unbound optional slot raises `NameError` when read. |
| `KOS_CACHE_MATCHER` | Per-site cache slot holding the thawed `Matcher` -- thaw-once, no guard/version (thaw is a pure function of the const) -- and a `KosSlotMap` (namespace subsystem) over the matcher's slot table: per slot the fast index or -1 for bind-by-name, guarded by the namespace layout with adoption of a same-contents fast-names tuple, so sibling function objects of one code share the resolution and another layout re-resolves. |
| Guards | `[[PATTERN] when:/unless: COND BODY...]`. Keep-flagged `MATCH_CASE` leaves the subject on the stack after binding; evaluate guard, `JUMP_FALSE`/`JUMP_TRUE` to the next arm (subject intact) or `POP` and commit. Captures persist on guard failure. |
| Externs | The special collects the frozen matcher's externs, `request_var`s each (closure capture works), and the handler loads them from the frame namespace at match time. |

Behavioural contracts: bindings land in the **enclosing scope** and persist
after the form (Python-consistent; arms are not isolated scopes); first arm
wins; no match + no `else:` → `None`; strings/bytes are atoms to sequence
patterns.

---

## API cheat sheet

```python
from koskari.matcher import (
    make_matcher, thaw, pattern_slot_names,
    MATCH, BIND, VALUE, BODY, DEFINE, BINDINGS, FORMALS, MatchCompiler,
)
from koskari.hygiene import (
    expand_template_with_hygiene, mark_expansion, peel_datums,
    finalize_expansion, candidates, Resolver, Deferred,
    HygieneFunctionSpec, function_constant, equal_by_base,
)
from koskari.types import strip_mark, strip_marks
from koskari.compile.helpers import match, match_pattern_raw
```

| Function | Role |
|----------|------|
| `make_matcher(pat)` | Compile a pattern |
| `pattern_slot_names(pat)` | Slot names in declaration order |
| `match(source, patterns)` | Try patterns; optional `with_specs` |
| `match_pattern_raw(source, patterns)` | First capture dict or `None` |
| `expand_template_with_hygiene(tpl, bindings, mark)` | Transcribe and scope |
| `mark_expansion(form, mark)` | Scope a procedural arm's output |
| `finalize_expansion(form, mark=None)` | Scope when given, then peel `Datum` |
| `strip_mark(atom)` / `strip_marks(form)` | The atom or form projected to bases |
| `Resolver(fragment, ancestors, module_names).resolve(fragment)` | Resolve scoped references at completion |
| `function_constant(compiler, parent, body, name, formals_spec)` | A body's FunctionSpec now, or a HygieneFunctionSpec when marked |
| `equal_by_base(left, right)` | Forms equal with every gensym read as its base |
| `parse_case_pattern(content)` | Lower one match/case clause delimiter |
| `parse_case_element(elem)` | Lower one case-pattern element |

---

## Tests and examples

| Area | Location |
|------|----------|
| Composites | `tests/matcher/test_composites.py` |
| Repeat frames | `tests/matcher/test_repeat_frames.py` |
| DatumFrame | `tests/syntax/test_syntax_bootstrap.py` (`TestDatumFrame`) |
| Template expansion | `tests/hygiene/test_hygiene.py` |
| The gensym type | `tests/lib/test_gensym.py` |
| Gensyms through the compiler | `tests/compile/specials/test_marked_forms.py` |
| Gensyms through the matcher | `tests/matcher/test_marked_subject.py` |
| Hygiene end to end | `tests/reproducers/test_mark_hygiene.py` |
| Special patterns | `koskari/compile/specials/` |
| Case-pattern dialect, `APP`, `MAP` | `tests/matcher/test_case_patterns.py` |
| `MATCH_CASE` opcode | `tests/lib/opcode/test_match_case.py` |
| `compile/match` (incl. guards, layout eval) | `tests/compile/specials/test_match.py` |
| `layout/match` structure | `tests/reader/layouts/test_match_layout.py` |

---

## Maintenance notes

When adding a new composite:

1. Implement `_expand()` as pure structure.
2. Register a surface factory in `register_builtin_matchers()` if exposed in
   `parse_matcher` / rule patterns.
3. Add tests for the match shape.

When writing a special that reads a name out of the form by hand, apply the
table under [Reading a marked atom](#reading-a-marked-atom): the key for a
binder or reference, `strip_mark` for anything compared by name. A missed
peel fails loud, as a NameError on a mangled name.
