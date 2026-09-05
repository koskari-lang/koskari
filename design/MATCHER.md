# Koskari matcher reference

Canonical reference for the native matcher (`koskari.lib.Matcher`), the Python
pattern builders in `koskari.matcher`, freeze-time hygiene plans
(`koskari.hygiene.Hygiene`), capture wrappers (`Datum`, `DatumFrame`), and the
**runtime match/case system** (the case-pattern dialect, the `MATCH_CASE`
opcode, and `compile/match` / `layout/match`).

**Implementation:** `koskari/matcher.py`, `koskari/hygiene.py`,
`koskari/syntax.py`, `koskari/lib/matcher.{c,h}`;
runtime match/case: `koskari/compile/specials/conditionals.py`,
`koskari/reader/layouts/conditionals.py`,
`koskari/lib/opcode/MATCH_CASE.c`

**Related design (deeper or historical):**

- [api/pattern-matching.md](api/pattern-matching.md) — tagged-tuple IR and migration notes
- [api/matcher-hygiene-extensions.md](api/matcher-hygiene-extensions.md) — hygiene discovery goals and macro modes
- [language/matcher-ng-rollup.md](language/matcher-ng-rollup.md) — rollout notes and as-built sketches
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

Two compilation paths exist:

| Compiler | Produces | Use when |
|----------|----------|----------|
| `MatchCompiler` | Frozen matcher only | Runtime matching, tests, patterns that do not need hygiene metadata |
| `HygienicMatchCompiler` | Frozen matcher **and** `Hygiene` plan | Compile-time specials, `Syntax` / `Transform` arms, anything that participates in `collect_binding_symbols` |

```python
from koskari.matcher import MATCH, BIND, DEFINE, BODY, HygienicMatchCompiler, make_matcher
from koskari.lib import Matcher

hc = HygienicMatchCompiler()
frozen = hc.lower(MATCH(DEFINE("name"), BODY("body")))
matcher = Matcher.from_tuple(frozen)
hygiene = hc.build_hygiene()
```

`make_matcher(pattern)` is shorthand for `MatchCompiler.compile(pattern)` (no
hygiene). `Pattern.build()` on a pattern object uses the same baseline compiler.

Special forms and syntax macros attach patterns via `@special(..., patterns=...)`
or `Syntax(...)`. Each winning arm stores `matcher` + `hygiene` on its
`Transform`, built with `HygienicMatchCompiler` in `koskari.syntax.Transform.__init__`.

---

## Frozen matcher IR

Lowering emits:

```text
("MATCHER", slot_names, extern_names, consts, nodes, entry_index)
```

- **Matching** and **thaw** use only this tuple; hygiene is never embedded in it.
- **Hygiene** is a separate structure (`Hygiene.to_tuple` / `from_tuple`, tag `HYGIENE`).
- `thaw(frozen)` and `Matcher.from_tuple(frozen)` reconstruct a native matcher.

`MatchResult.captures` is a **sparse** dict: only slots that actually bound appear.
Check membership before use.

---

## Datum and DatumFrame

### Datum

`koskari.lib.Datum` is the hygiene **syntax box**: a native wrapper around a
captured value with optional source position and flags.

Captures are boxed only on a walk over source: a `KosPair` spine (cell
positions travel with the value) or a walk inside a substitution `Datum`
(`from_datum` must travel). A tuple, list, or single-value subject yields
raw values in `captures`, including REST tails, multi-cell slices, and
REPEAT rows. The pattern parsers (`parse_matcher`, `parse_case_element`,
`parse_case_pattern`) hand their source element in as a one-cell pair
spine for this reason.

| Field / property | Role |
|------------------|------|
| `.datum` | Wrapped value (symbol, pair spine, list, etc.) |
| `.position` | Source position when matched from `KosPair` cells; else `None` |
| `.from_datum` | When true, the capture came from **substitution** (pattern variable), not template introduction |

**Macro expansion:** substituted pattern-variable values are stored as `Datum`
unchanged. Hygiene renaming must not descend into those cells.

**Binder discovery:** `iter_introduced_symbols` on composites skips captures
where `_intro_suppressed(capture)` is true (`Datum` with `from_datum=True`).

**Region walks:** `collect_binding_symbols` treats a bare `Datum` node as
opaque (returns immediately). `iter_recurse_targets` yields are skipped when
the value is a `Datum` instance.

**Finalize:** `finalize_macro_hygiene_unbox_one_pass` peels `Datum` for compile
without gensymming substituted interiors.

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
- `.from_datum` — propagated when the frame represents substituted material.

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

Capture options on named captures (not matched input):

```scheme
(scope: body)
(mode: parallel)    ; BINDINGS only at factory level
(role: N)             ; stored on CAPTURE wire; reserved
```

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

| Shorthand | Expands to | Hygiene |
|-----------|------------|---------|
| `BIND(slot)` | `CAPTURE(slot)` | None |
| `SYMBOL(slot)` | `CAPTURE(slot, TYPE("symbol"))` | None |
| `KEYWORD(slot)` | `CAPTURE(slot, TYPE("keyword"))` | None |
| `PAIR(slot)` | `CAPTURE(slot, TYPE("pair"))` | None |
| `VALUE(slot)` | `CAPTURE(slot)` + **scope region** | See below |
| `BODY(slot)` | `REST(slot, min_tail=1)` + **scope region** | See below |

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

Hygiene is discovered from **the same pattern** used to match a special form,
without hard-coding form names in `koskari.hygiene`.

### Freeze time

`HygienicMatchCompiler` records:

- **Binders** — slots whose patterns introduce names (`DEFINE`, `BINDINGS`, …).
- **Regions** — slots whose captures are walked as lexical extent (`VALUE`, `BODY`).
- **Scoped slots** — region slots referenced by a binder’s `scope:` / `scopes:`;
  walked via the binder’s `_scopes`, not as standalone regions.
- **Edges** — `group_child` (nested `GROUP`), `repeat_element` (`repeat_capture` lists).

`Pattern.visit` + `on_pattern` call `register_hygiene` on objects implementing
`HygienicPattern`.

### Runtime walk (`walk_hygiene_tree`)

For each hygiene node on a successful match:

1. **Binders** — `iter_introduced_symbols` → add to `binding_symbols`.
2. **Binders** — `iter_recurse_targets` → `collect_binding_symbols` on each
   (initializer expressions), skipping `Datum`.
3. **Binders** — `_scopes` → walk sibling capture slots (e.g. let body).
4. **Regions** — `iter_scope_targets` → `_collect_from_region_root` on each
   (unless slot is only in `scoped_slots`).
5. **Edges** — recurse into nested `GROUP` dicts or `REPEAT` frame lists.

`_collect_from_region_root` treats a spine whose `car` is a resolved compile
head as **one application** and calls `special.match_for_hygiene` on it;
otherwise it walks each top-level sibling.

### Match-time vs hygiene-time

| Concern | Mechanism |
|---------|-----------|
| Does the form shape match? | Native `Matcher.match` |
| What names does this form introduce? | `Hygiene` + `walk_hygiene_tree` |
| What code is in scope for those names? | Region walks + binder `scope:` |
| What must not be renamed? | `Datum` / `from_datum` |

---

## Hygiene reference by matcher

Legend:

- **Capture** — binds `captures[slot]` (or nested dict keys).
- **Introduce** — symbols added to `binding_symbols` for gensym rename.
- **Recurse** — nested forms scanned for inner binders (not renamed here).
- **Region walk** — `iter_scope_targets`; full subtree hygiene via
  `collect_binding_symbols` / nested specials.
- **Scoped via binder** — slot listed in composite `scope:` / `scopes:`; walked
  from binder, not registered as a standalone region.

### Primitives and shorthands

| Matcher | Capture | Introduce | Recurse | Region walk |
|---------|---------|-----------|---------|-------------|
| `BIND` / bare `CAPTURE` | One element | No | No | No |
| `SYMBOL` | One symbol | No | No | No |
| `KEYWORD` | One keyword | No | No | No |
| `PAIR` | One pair | No | No | No |
| `REST` | Tail spine | No | No | No |
| `VALUE` | One element | No | No | **Yes** — entire capture |
| `BODY` | Tail (`min_tail=1`) | No | No | **Yes** — entire capture as one extent |
| `MATCH`, `FORM`, `LITERAL`, … | Via children only | Via children | Via children | Via children |

### Composites

| Composite | Capture result | Introduce | Recurse | Region / scope |
|-----------|----------------|-----------|---------|----------------|
| `DEFINE(slot)` | Symbol in `slot` | **Yes** — `slot` symbol | No | Optional `scope:` / `scopes:` on sibling slots |
| `DECL(slot)` | Dict / `DatumFrame`: `name`, `type`, `decl_tail`, … | **Yes** — declaration name | **Yes** — `type`, keyword tail pairs | Optional `scope:` |
| `BINDING(slot)` | Dict / `DatumFrame`: `name`, `expr` | **Yes** — `name` | **Yes** — `expr` | Optional `scope:` |
| `BINDINGS(slot)` | `nil` or **list** of row dicts | **Yes** — each row `name` | **Yes** — each row `expr` | Optional `scope:`; `mode:` stored, **not interpreted** yet |
| `FORMALS(slot)` | Dict / `DatumFrame`: `positionals`, `defaults`, `star`, `kwonly`, `starstar`, `by_refs` | **Yes** — param names, `*`, `**` | **Yes** — default / attr value expressions | Optional `scope:`; `&`-prefixed names recorded in `by_refs` |
| `USING(slot)` | Dict / `DatumFrame`: `lhs`, `name`, `expr` (arm-dependent) | **Yes** — `name` when present | **Yes** — `expr` | Optional `scope:` |
| `USING_UNPACK(slot)` | Dict / `DatumFrame`: `lhs`, `expr` | **Yes** — symbols from unpack `lhs` | **Yes** — `expr` | Optional `scope:` |

`DEFINE` does **not** imply hygienic rewriting of a sibling value slot; only the
defined **name** is introduced. Use `VALUE` for RHS slots that must be walked for
nested binders, or `BIND` when the slot is opaque data (literal lists, quasiquote
fragments, etc.).

### Typical special-form shapes

```python
# define: name introduced; value walked for nested binders
_MATCH_DEFINE = MATCH(DEFINE("name"), VALUE("value"))

# let: parallel bindings; body is scope region
_MATCH_LET = MATCH(
    BINDINGS("bindings", mode="parallel", scope="body"),
    BODY("body"))

# lambda: formals introduce; body walked
_MATCH_LAMBDA = MATCH(
    FORMALS("formals", scope="body"),
    BODY("body"))
```

```python
# syntax-rules spec: name introduced; literals are data; rules tail is not a body
_SYNTAX_RULES = MATCH(
    SYMBOL("_"),           # head ignored for hygiene (not DEFINE)
    DEFINE("name"),
    BIND("literals"),       # not VALUE — do not region-walk literal list
    REST("rules"))
```

Use `BIND` (not `VALUE`) for the literals position so hygiene does not descend
into `(syntax-rules sym …)` embedded inside the literals list. A separate
collector issue can still mis-classify such a spine as a full
`compile/syntax-rules` form when walked as a compile head; fixing that may
require collector guards, not matcher syntax alone.

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
  (never on `*` / `**`). See [SYNTAX.md](../SYNTAX.md) (By-ref formals).
- **Compile:** `gather_formals` in `koskari.compile.helpers`.

### USING / USING_UNPACK

- **USING:** ordered `ONEOF` of unpack, name+expr, as-form, bare expr.
- **USING_UNPACK:** only `((pattern…) expr)`.

---

## Syntax integration

### `Syntax` and `Transform`

Each transform arm:

1. Lowers `pattern` with `HygienicMatchCompiler`.
2. Stores `matcher` + `hygiene` on the arm.
3. `try_expand` runs `matcher.match(source_obj)` then `handle_expand`.

`Syntax.match_for_hygiene(form)` returns `(captures, pattern, hygiene)` for the
first matching arm, or `(None, None, None)`.

### `collect_binding_symbols`

Used during macro finalize (`koskari.syntax._finalize_macro_output`):

1. Walk template / expanded form pair tree.
2. On compile head, call `special.match_for_hygiene`.
3. If a hygiene plan exists, `walk_hygiene_tree`.
4. Else fall back to element-wise descent.

### Template expansion

`syntax-rules` uses matcher-driven binding discovery and
`expand_template_with_hygiene`. Substituted subtrees remain `Datum`-boxed;
introduced template identifiers are gensymmed.

---

## Choosing a matcher (quick guide)

| Intent | Use |
|--------|-----|
| Match and bind any single cell | `BIND` |
| Match a symbol cell | `SYMBOL` |
| Match a symbol that **introduces** a binding | `DEFINE` |
| Match RHS / initializer and find binders inside | `VALUE` |
| Match statement tail / lambda body extent | `BODY` |
| Match tail without hygiene registration | `REST` |
| Match literal identifier list / opaque subform | `BIND` (not `VALUE`) |
| Match binding list for let | `BINDINGS(..., scope="body")` |
| Match formals list | `FORMALS(..., scope="body")` |
| Match exact atom | `LITERAL` / `LITERAL_SYM` / `LITERAL_KW` |
| Repeat rule elements | `repeat_capture` + `REPEAT` |

**Common mistake:** `VALUE("literals")` on a slot that holds a **list of symbols**
(cannot contain binders). Hygiene will still **region-walk** the list and may treat
nested `(syntax-rules …)` spines as full specials. Prefer `BIND("literals")`.

**Doc vs implementation:** older text sometimes describes `VALUE` as “same as
`BIND` but not a scope region.” In the current code, `VALUE` **registers a scope
region** and is the right choice when you **want** descent (e.g. `compile/define`
value arm). `BIND` is the right choice when you do not.

---

## ONEOF, GROUP, and hygiene nesting

- **ONEOF** branches use separate capture scopes; duplicate slot names across
  arms are allowed.
- **GROUP(slot, …)** with a name creates a `group_child` hygiene edge so nested
  binders inside the group are discovered in a child hygiene node.
- **REPEAT** over `GROUP(None, row)` links `repeat_element` edges when lowered
  under `HygienicMatchCompiler` with `note_repeat_capture`.

---

## MATCH dialects (three parsers)

| Parser | Input | Typical use |
|--------|-------|-------------|
| `parse_matcher` | One pattern element | Python special `patterns=`, nested constraints |
| `parse_explicit_rule_pattern` | Proper list spine | `syntax-rules` / `syntax-match` rule patterns |
| `parse_case_pattern` / `parse_case_element` | One match/case clause delimiter / one case element | Runtime `match`/`case` patterns (see the runtime section below) |

Surface heads and capture options are shared; rule-level parsing sequences cells
and supports `(BINDINGS bindings mode: parallel)` declarations. The case
dialect adds the reader-sugar heads and delegates marked / registry-headed
forms back to `parse_matcher`.

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
| `MATCH_CASE` | EXTRA row: no-match label, frozen matcher const, externs name tuple, keep-subject flag; one `KOS_CACHE_MATCHER` slot. Subject held on the stack across arms; wrapped as a length-1 sequence per attempt. No match: subject stays, jump. Match: the engine fills a slot array aligned to the matcher's slot table (`KosMatcher_RunSlotsVectorcall`), the opcode binds each slot that fired into the namespace and pushes nothing (subject consumed unless `keep`). The subject is a single runtime value, never a source spine, so captures are raw. **Unfired slots are skipped** — an unbound optional slot raises `NameError` when read. |
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
    MATCH, BIND, VALUE, BODY, DEFINE, BINDINGS, FORMALS,
    HygienicMatchCompiler,
)
from koskari.hygiene import (
    collect_binding_symbols,
    walk_hygiene_tree,
    introduced_symbols_from_match,
)
from koskari.compile.helpers import match, match_pattern_raw
```

| Function | Role |
|----------|------|
| `make_matcher(pat)` | Compile without hygiene |
| `HygienicMatchCompiler().lower(pat)` | Frozen tuple + optional `build_hygiene()` |
| `pattern_slot_names(pat)` | Slot names in declaration order |
| `match(source, patterns)` | Try patterns; optional `with_specs` |
| `match_pattern_raw(source, patterns)` | First capture dict or `None` |
| `collect_binding_symbols(form, find_compiled)` | Binding symbols for finalize |
| `introduced_symbols_from_match(matched, hygiene)` | Binders only, no `find_compiled` |
| `parse_case_pattern(content)` | Lower one match/case clause delimiter |
| `parse_case_element(elem)` | Lower one case-pattern element |

---

## Tests and examples

| Area | Location |
|------|----------|
| Composites | `tests/matcher/test_composites.py` |
| Repeat frames | `tests/matcher/test_repeat_frames.py` |
| DatumFrame | `tests/syntax/test_syntax_bootstrap.py` (`TestDatumFrame`) |
| Hygiene / finalize | `tests/hygiene/test_hygiene.py` |
| Special patterns | `koskari/compile/specials/` |
| Case-pattern dialect, `APP`, `MAP` | `tests/matcher/test_case_patterns.py` |
| `MATCH_CASE` opcode | `tests/lib/opcode/test_match_case.py` |
| `compile/match` (incl. guards, layout eval) | `tests/compile/specials/test_match.py` |
| `layout/match` structure | `tests/reader/layouts/test_match_layout.py` |

---

## Maintenance notes

When adding a new composite:

1. Implement `_expand()` as pure structure.
2. If it introduces binders, implement `iter_introduced_symbols` and
   `register_hygiene` → `add_binder` (or inherit `HygieneWireSlotScopes`).
3. If initializers or types need discovery, implement `iter_recurse_targets`.
4. If a body slot should be in scope, take `scope=` / `scopes=` and document which
   sibling slot (`BODY`, `REST`, etc.) must be registered as a region or scoped slot.
5. Register a surface factory in `register_builtin_matchers()` if exposed in
   `parse_matcher` / rule patterns.
6. Add tests for both match shape and hygiene discovery.

Keep hygiene metadata out of the frozen matcher tuple; extend `Hygiene` / composites
only when discovery rules change.
