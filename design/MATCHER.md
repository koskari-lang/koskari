# Koskari matcher reference

Canonical reference for the matcher: the Python pattern builders in
`koskari.matcher`, the tables, analysis and emitter
(`koskari.matcher.tables`, `.analysis`, `.emitter`) that lower a pattern to
`MATCH_*` instructions, the matcher functions `koskari.matcher.build`
completes and the first-match function a `Syntax` tries its arms through,
the capture wrappers (`Datum`, `DatumFrame`), the
scoped symbols (`koskari.lib.gensym`) that carry macro hygiene, and the
**runtime match/case system** (the case-pattern dialect and `compile/match`
/ `layout/match`).

**Implementation:** `koskari/matcher/`,
`koskari/hygiene.py`, `koskari/syntax.py`, `koskari/lib/matcher.{c,h}`,
`koskari/lib/matcher/step.c`, `koskari/lib/eval/eval_match.c`,
`koskari/lib/opcode/MATCH_*.c`; runtime match/case:
`koskari/compile/specials/match.py`,
`koskari/reader/layouts/conditionals.py`

**Related design (deeper or historical):**

- [archive/matcher-core.md](archive/matcher-core.md) -- the matcher core design: every node against a cursor, captures in marks on the operand stack, the instruction set and the per-node lowering (implemented)
- [archive/matcher-callable.md](archive/matcher-callable.md) -- the matcher as the `Function` its lowering builds (implemented)
- [archive/matcher-bytecode.md](archive/matcher-bytecode.md) -- the first lowering to bytecode, which the core replaced (implemented)
- [api/pattern-matching.md](api/pattern-matching.md) -- the pattern-authoring API and its design notes
- [archive/matcher-hygiene-extensions.md](archive/matcher-hygiene-extensions.md) -- the superseded discovery-by-pattern hygiene design
- [archive/matcher-ng-rollup.md](archive/matcher-ng-rollup.md) -- rollout notes and as-built sketches from before hygiene by marks; the hygiene names it uses are gone
- [archive/macro-hygiene-syntax-boxes.md](archive/macro-hygiene-syntax-boxes.md) -- `Datum` in macro expansion
- [language/unified-matcher-transformer-model.md](language/unified-matcher-transformer-model.md) -- specials, syntax-rules, and shared finalize pipeline
- [archive/match-pattern-language.md](archive/match-pattern-language.md) — runtime pattern-language design record
- [archive/match-case-form.md](archive/match-case-form.md) — the retired MATCH_CASE opcode and its cache, a design record
- [language/define-matcher.md](language/define-matcher.md) — parameterized matcher templates (open design)

---

## Overview

The matcher is a small pattern language over **spine subjects**: proper lists
of koskari source cells (usually `KosPair`) at compile time, or **runtime
sequences** (tuple, list) at run time. The same lowering serves both: macro /
special-form dispatch matches source forms, and the runtime `match`/`case`
statement matches values. Patterns are built in Python (`MATCH(...)`,
`BINDINGS(...)`, …) or parsed from surface syntax. `TableBuilder` lowers a
pattern to its node tables, `analyse` computes what the emitter needs and
refuses at build what the core does not lower, and the `Emitter` lowers the
tables to `MATCH_*` instructions ([OPCODES.md](OPCODES.md) section 9.2). The
instructions live in one of three containers:

- a standalone `Function`, for a pattern built in Python (`Pattern.build`,
  `make_matcher`): its body is completed by one shared compiler and bound to
  a namespace whose builtins are the externs given to the build, and a call
  with the subject returns the captures or `None`;
- a lambda the defining module's compiler builds, for a `syntax-rules` or
  `syntax-match` rule, its externs closure names of the module, sealed with
  the module's code;
- the enclosing function itself, for a `match`/`case` arm, its root captures
  stored into the function's variables.

```python
from koskari.matcher import MATCH, DEFINE, BODY, make_matcher
from koskari.matcher.analysis import analyse
from koskari.matcher.tables import TableBuilder

tables = TableBuilder().lower(MATCH(DEFINE("name"), BODY("body")))
analysis = analyse(tables)
matcher = make_matcher(MATCH(DEFINE("name"), BODY("body")))
```

`make_matcher(pattern)` and `Pattern.build()` are the same call. A pattern
carries no hygiene metadata: which names a form binds is the handler's
business, and hygiene is provenance on the atoms themselves (see
[Hygiene model](#hygiene-model)).

Special forms and syntax macros attach patterns via `@special(..., patterns=...)`
or `Syntax(...)`. Each arm stores its matcher function on its `Transform`,
built in `koskari.syntax.Transform.__init__` or compiled by the defining
module; the `Syntax` tries the arms through `first_match`.

---

## Node tables

Lowering emits:

```text
(slot_names, extern_names, consts, nodes, entry_index)
```

- A `TEST` node is `ANY`, `LITERAL` (a const index), `TYPE` or
  `PREDICATE` (an extern index), or `CORE` (a `CoreType` ordinal: one of
  `pair`, `symbol`, `keyword`, `int`, `str`, `tuple`, `list`, which
  `TYPE(name)` lowers to without interning an extern).
- `analyse(tables, arm=False)` gives the emitter the widths, the length
  prechecks, the capture scopes and slot indices, and the slots of each
  alternative, and refuses at build: a non-pool constant, a REPEAT body
  that may consume nothing, a REST followed on its cursor by another REST
  or a run of variable width, a CAPTURE under NOT, a slot bound twice in
  one scope, a positional FIELD after a keyword FIELD, a SAME in a match
  arm, and a bound past the 16-bit field limit.
- The tables are an in-memory handoff; nothing serializes or thaws them.

`MatchResult.captures`, from `Pattern.match`, is a **sparse** dict: only
slots that actually bound appear. Check membership before use.

---

## Datum and DatumFrame

### Datum

`koskari.lib.Datum` is the box the matcher puts input material in: a
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

**Not everything is a DatumFrame:** `BINDINGS` stores a bare Python `list`,
one dict per binding (or `nil` for the empty arm). A `repeat_capture` list
holds one entry per iteration: a dict, or a `DatumFrame` on a KosPair
subject.

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

Used for **syntax-match / macro-match** rule patterns (MATCH dialect,
`koskari/matcher/heads.py`); syntax-rules and macro-rules take the RULE dialect
through `parse_syntax_rules_pattern` in `koskari/syntax.py`:

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

These lower to nodes of the node tables.

| Pattern | Match behaviour | Capture |
|---------|-----------------|---------|
| `TEST_ANY()` | Any one spine element | No |
| `SAME(slot, depth=0)` | One element equal to the value bound to `slot` in the capture scope `depth` levels out (0 the scope the SAME sits in: the root, a GROUP or a REPEAT pass); when no such scope binds `slot` it always misses. `DatumFrame` operands are unsupported (match fails). Duplicate `BIND`/`CAPTURE` for the same slot is refused at build -- use `SAME`. RULE patterns lower repeats to `SAME`. Refused in a match arm. | No |
| `LITERAL(v)` | Equal to `v` | No |
| `TYPE(extern)` | Instance of the extern type; a core type name (`symbol`, `pair`, `keyword`, `int`, `str`, `tuple`, `list`) lowers to `CORE` with no extern | No |
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
| `FIELD(pat)` / `FIELD(pat, name=n)` | One positional field (by `__match_args__`) or the named attribute | Inner slot |
| `APP(fn, sub=None)` | Call extern `fn` on the element, match `sub` against the result in a projected single-element subject; a raising extractor propagates | Via `sub` |
| `MAP(MAP_ENTRY…)` | One mapping element (dict or `collections.abc.Mapping`); each entry's key must be present with value matching the entry's sub-pattern; extra keys ignored (PEP 634) | Via entry sub-patterns |
| `MAP_ENTRY(key, sub=None, extern=False)` | One MAP entry; key is a const, or an extern name resolved at match time when `extern` | Via `sub` |

**Callable externs.** A `PREDICATE` or `APP` extern is a `CALL` from the
function that matches, so a Koskari callable flattens into the running
evaluator and one that awaits does so in place, as any call does
([FLATTENING.md](FLATTENING.md)).

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
  unmarked. A template `elem ...` repeats once per element of the repeat
  sequences the element names; an element naming none raises
  `CompilerError` (it would transcribe to nothing). The escape
  `(... TEMPLATE)` transcribes `TEMPLATE` with every ellipsis as an
  element of the output, so an outer template can hold an inner
  `syntax-rules` whole; the escape itself is consumed, and a nested
  escape inside it is literal too.
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
name. A keyword-named parameter is the one binder hygiene does not scope.

### Raw forms in templates

A template pair headed by the raw `datumq` symbol is a directive
(`_is_datumq_directive` in `koskari.hygiene`). `(datumq X)` transcribes
to `Datum(X, position)`, the position being the spine cell that holds
`X`; it is not marked and not entered, so a pattern variable inside is
not substituted, and `_collect_symbols` skips it so a name inside never
drives an ellipsis. Any other arity is a `CompilerError`. The head is
matched by identity, so a transcription consumes its own directives and
an inner template sees only raw ones: `((datumq datumq) ...)` emits a
literal call, and `(datumq (datumq X))` reaches a nested template as
`(datumq X)`. `-'X` reads as `(datumq X)`. `syntax-rules` and
`syntax-match` share the path; in a procedural arm `(datumq X)` is the
bootstrap macro, which yields the same `Datum` at run time.

### Reading a scoped atom

A gensym reaches the compiler wherever the template symbol would have, and
every symbol test accepts it. The rule at each site:

| The atom is read as | Do |
|---------------------|----|
| a binder or a reference | pass the atom; the processor spells it |
| a head in call position | `find_compiled` tries the spelling's candidates, then the bare name |
| a tag, label, literal, attribute, import member, operator | `.base`, or `strip_mark` when the operand may not be an atom |
| quoted data | `quote` / `quasiquote` project to the base; `quote-syntax` keeps the gensym |
| a name shared with the use site | the template writes `(datumq name)`, or `-'name` |

`koskari.types.strip_mark` projects one value, `strip_marks` a whole form;
`type(x) is gensym` is the exact-type test, and a known atom reads `.base`.
Handlers that go through `gather_formals`, the unpack helpers,
`compile_symbol`, and the fragment's `request_var` / `declare_var` get
the key for free; a handler that reads a name by hand applies the table.

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

1. Builds its matcher function from `pattern` with `Pattern.build`, or
   takes the function a module compiled for the rule (the `build-*`
   builtins' function rows).
2. `try_expand` calls the matcher function on `source_obj` and, on a hit,
   `handle_expand` with the captures.

The `Syntax` itself does not try the arms one by one in Python. It calls
`first_match`, the hand-written Koskari function `FIRST_MATCH`
(`koskari.matcher.build`), with the form and the tuple of its arms'
matcher functions: each is called in order, flattening, and the first
result that is not `None` returns with its index; `(-1, None)` when no arm
matches. `compile_impl` makes one call per form compiled. Composition with
`+` makes a new `Syntax` over the combined arms.

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

The runtime `match` statement is the value-matching consumer of the
lowering: `compile/match` (s-expr) and `layout/match` (block syntax) lower
each arm's pattern to its node tables, analyse them as an arm, and emit its
instructions inline, through `lower_arm`, in the enclosing function.

### The case-pattern dialect

`parse_case_pattern(delimiter_content)` classifies one clause's pattern;
`parse_case_element(elem)` classifies one element. Both are meta-matchers plus
walkers, the same architecture as `parse_matcher` (`CASE_ELEMENT_MATCHER`,
`CASE_DELIM_MATCHER` in `koskari/matcher/case.py`). Pattern *kind* is decided by
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
| `Point[x y]` | `(item Point x y)` | `CLASS`; Python field rules (`__match_args__` positional, `k: pat` keyword, self-binding bare symbol after keywords). On 3.9 (pre-PEP 634) the class test synthesizes `__match_args__` for dataclasses / namedtuples, gated `PY_MINOR_VERSION < 10` so a 3.10+ `match_args=False` opt-out is never overridden |
| `(app f PAT)` | head `app` | `APP` |
| `(? pred)` | head `?` | `PREDICATE` |
| `(or ...)` / `(not ...)` | heads | `ONEOF` / `NOT` |
| `(REST r)`, `(CLASS ...)`, any registry head | registry passthrough | via `parse_matcher` |
| bare multi-token list `(a b c)` | — | **error**; sequences are always the explicit `#`-form |

The clause shape is `[[PATTERN] BODY...]` — the pattern rides in its own
delimiter list, which holds one pattern form or the multi-token `::` spelling.
The delimiter is a pure container: the special reads it with `pair`/`nil` only.

### Instructions and lowering

| Piece | Behaviour |
|-------|-----------|
| The arm | The subject stays on the stack across the arm chain. `lower_arm` emits the arm's `MATCH_*` instructions in place: a root that is one element test tests the subject in place; any other root walks a one-element cursor over a copy. A miss jumps with the subject on the stack to the next arm whose outcome is not yet known: an arm whose gate, its first type test, misses goes past the following arms whose gate tests the same core type or class variable, and an arm that misses after a passed `CORE` gate enters the next arm just past its same `CORE`. A hit consumes the subject and falls into the arm body; the arm's fail entries (the pops down to the subject, the jump) are emitted after the body. |
| Captures | A root capture is stored into its variable of the enclosing function as soon as its value is tested, so a missed arm may leave variables it stored; a capture inside a GROUP or REPEAT is reached through that node's value. Binding names are declared in the enclosing scope. Only a slot that fired is written; an unbound optional slot raises `NameError` when read. The subject is a single runtime value, never a source spine, so captures are raw. |
| Guards | `[[PATTERN] when:/unless: COND BODY...]`. The guard compiles after the arm's instructions with the subject still on the stack; `JUMP_FALSE`/`JUMP_TRUE` to the next arm (subject intact) or `POP` and commit. Captures persist on guard failure. |
| Externs | Every name the pattern resolves is requested through `request_var` and read from the enclosing scope, so closure capture works and a store into a user type is seen. A predicate or extractor is a `CALL` from the same frame and flattens; one that awaits does so in place. |
| Core types | `int`, `str`, `list`, `tuple`, `pair`, `symbol` and `keyword` are not externs: a test against one is a `MATCH_CORE` instruction carrying the type's ordinal, with no reference read, so a binding of the name in any scope never reaches a pattern. `CALLEDBY`, `SYMBOL`, `KEYWORD`, `PAIR`, `BINDINGS` and `FORMALS` rest on these, which is what lets a module bind `symbol` without breaking the macros it defines. |

Behavioural contracts: bindings land in the **enclosing scope** and persist
after the form (Python-consistent; arms are not isolated scopes); first arm
wins; no match + no `else:` → `None`; strings/bytes are atoms to sequence
patterns.

---

## API cheat sheet

```python
from koskari.matcher import (
    make_matcher, first_match, pattern_slot_names, MatchResult,
    MATCH, BIND, VALUE, BODY, DEFINE, BINDINGS, FORMALS,
)
from koskari.matcher.analysis import analyse
from koskari.matcher.build import build_matcher
from koskari.matcher.tables import TableBuilder
from koskari.hygiene import (
    expand_template_with_hygiene, mark_expansion, peel_datums,
    finalize_expansion, candidates, Resolver, Deferred,
    HygieneFunctionSpec, function_constant, equal_by_base,
)
from koskari.types import strip_mark, strip_marks
```

| Function | Role |
|----------|------|
| `make_matcher(pat, externs=None)` | The pattern's matcher `Function`; `Pattern.build(externs)` is the same call |
| `Pattern.match(subject, **externs)` | Build and run the matcher; a `MatchResult` |
| `first_match(subject, matchers)` | Call each matcher function in order; the first hit's index and captures, or `(-1, None)` |
| `TableBuilder().lower(pat)` | The pattern's node tables |
| `analyse(tables, arm=False)` | The tables' `Analysis`; raises on a pattern the core refuses |
| `build_matcher(tables, analysis, ns)` | The tables as a `Function` over `ns` |
| `pattern_slot_names(pat)` | Slot names in declaration order |
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
| `MATCH_*` instructions | `tests/lib/opcode/test_match_instructions.py` |
| Analysis and refusals | `tests/matcher/test_core_analysis.py` |
| Differential corpus against master | `tests/matcher/test_oracle.py` |
| The processor's matcher fusions | `tests/compile/test_fuse_feeders.py` |
| The compiled function's capture shapes | `tests/matcher/test_match_function.py` |
| The `Syntax` first match | `tests/syntax/test_syntax_dispatch.py` |
| `compile/match` (incl. guards, layout eval) | `tests/compile/specials/test_match.py` |
| `layout/match` structure | `tests/reader/layouts/test_match_layout.py` |

---

## Maintenance notes

When adding a new composite:

1. Implement `_expand()` as pure structure.
2. Register a surface factory in `register_builtin_matchers()` if exposed in
   `parse_matcher` / rule patterns.
3. Add tests for the match shape.

A composite needs no change below the node tables. A new primitive node
kind needs its node in `TableBuilder`'s lowering, its width and refusals in
`koskari/matcher/analysis.py`, a lowering in `koskari/matcher/emitter.py`,
and, when no existing instruction does its test, an instruction: the `OP`
row in `opcodes.incl`, the step in `koskari/lib/matcher/step.c`, the body in
`koskari/lib/eval/eval_match.c`, the thin handlers, and a `_VAR` / `_FAST`
twin pair when it reads a variable, which `convert_fast` pairs by name.

When writing a special that reads a name out of the form by hand, apply the
table under [Reading a scoped atom](#reading-a-scoped-atom): the key for a
binder or reference, `strip_mark` for anything compared by name. A missed
peel fails loud, as a NameError on a mangled name.
