# Koskari Unified Matcher Rollup (matcher)

## Status

Archived. The hygiene machinery this rollup names, `HygienicMatchCompiler`,
`build_hygiene`, `collect_binding_symbols`, and the finalize helpers, was
replaced by hygiene by marks; see [gensym-atom-subtype.md](gensym-atom-subtype.md)
and the [MATCHER.md](../MATCHER.md) rollup for the current shape.

Phases 1 through 6 of the implementation strategy below are landed. The
native engine lives in `koskari/lib/matcher.{c,h}`; the Python builder
API and the cleanroom composite layer live in `koskari/matcher.py`.
`matcher` does **not** import **`koskari.exceptions`**, arbitrary
**`koskari.compile`** submodules, or the removed legacy **`koskari.matcher`**
module, and there is no post-match Python transform
layer on the engine itself: `Matcher.match` returns the native
`MatchResult` directly; captures are final. The frozen matcher constant
is a **6-tuple** consumed by `Matcher.from_tuple` (see `matcher_from_tuple`
in `koskari/lib/matcher.c`); **hygiene** is an optional parallel
**`Hygiene`** object from `HygienicMatchCompiler.build_hygiene()`, not
part of that tuple or of `MatchResult` (see "As-built notes").

**Python dependencies (as-built):** `matcher` imports **`Hygiene`**
from `koskari/hygiene.py` (for `HygienicMatchCompiler` metadata) and
shape helpers from **`koskari.types`**, in addition to **`koskari.lib`**
(`Datum`, `Matcher`, `MatchResult`, `symbol`, `pair`, `nil`, `keyword`,
etc.). So the builder layer is cleanroom relative to the **removed** legacy
matcher / compile-helper stacks, but not a single-module island.

Phase 6 parser/registry surface is landed in `matcher` as
`META_PATTERN`, `META_MATCHER`, `parse_matcher`,
`MATCHER_REGISTRY`/`register_matcher`/`lookup_matcher`, and built-in
head factory registration. Coverage for this surface lives in
`tests/matcher/test_syntax.py`.

**Phases 7–9 (re-scoped).** Compile-time **syntax-rules / macro-rules /
special-rules** dispatch for **`compile/define-syntax`** lowers rule surfaces
in **`koskari/compile/specials/syntax.py`** into quoted runtime call forms
(**`build-syntax`**, **`build-macro`**, **`build-special`**) backed by
**`parse-rule-pattern`** / **`parse-match-pattern`**; see bootstrap bindings in
**`koskari/importer/bootstrap.py`**. **Runtime `match/case` (Phase 8) is
landed** (see the Phase 8 section below and the archived design docs). What
remains for this rollup is narrower than the original Phase 7 bullet:
per-special polish where a form still prefers ad hoc matching over shared
**`matcher`** / **`helpers`** surfaces, and optional **rename NG modules to
canonical names** once the tree stabilizes. Earlier legacy matcher stacks and duplicate compile-helper paths
that predated this layout are **removed**. Treat Phase 7 in the checklist below as **landed** for the define-syntax /
**`syntax.Syntax`** story, with follow-up only for straggler specials and
hardening.

**Syntax NG (dispatcher and transforms, landed and wired).**
`koskari/syntax.py` defines **`Syntax`**, **`SyntaxTransform`**, **`MacroTransform`**, and
**`SpecialTransform`**, plus runtime builtins **`build-syntax`**, **`build-macro`**, and
**`build-special`**, with **`parse-rule-pattern`** and **`parse-match-pattern`** lowering
RULE vs MATCH pattern datums at the use site. **`koskari/compile/specials/syntax.py`**
emits quoted call forms (for example **`(build-syntax NAME-STR (parse-rule-pattern …) (compile/quote TEMPLATE) …)`**)
rather than constructing **`Syntax`** during compilation; evaluation of those forms
produces the **`Compiled`** transformer. Bootstrap wires the hyphenated names into the
runtime (see **`koskari/importer/bootstrap.py`**).

* **`Syntax` (`Compiled`)** — ordered **transform** arms backed by a tuple of thawed
  **`Matcher`** instances; **`compile_impl`** uses **`match_select`** on the full
  **`source_obj`**, then **`handle_expand`** on the winning arm; total miss raises
  **`SpecialError`**. **`__add__`** prepends clauses. **`match_for_hygiene`** uses the
  same **`match_select`** and returns **`(captures, pattern, hygiene)`** for the
  winning arm, or **`(None, None, None)`** when no arm matches.
* **`Datum`** (`koskari.lib.Datum`) — native match box wrapping the captured
  value, source **`position`**, and optional **`binding`**. Template expansion
  and **`finalize_macro_hygiene_unbox_one_pass`** peel **`Datum`** layers where
  needed for compile. **`matcher`** hygiene walks keep **`Datum`** on
  recurse/scope yields where the engine produced them.
* **`Transform` (`typing.Protocol`, `@runtime_checkable`)** — structural
  contract for arms; **`__init__(pattern)`** requires a matcher **`Pattern`**,
  runs **`HygienicMatchCompiler.lower`** / **`build_hygiene()`**, and caches a
  thawed **`Matcher`** as **`self.matcher`**. **`try_expand`** runs
  **`self.matcher.match(subject)`** and delegates to **`handle_expand`** on success;
  no per-call Python "engine extern" dict is passed—the native defaults cover
  **`symbol`** / **`pair`** / **`cons`** / **`nil`** / **`keyword`** for KosPair
  subjects.
* **`SyntaxTransform`** — template-only arm (**`pattern`**, **`template`**, optional
  **`pattern_src`**); after match, string-keyed captures become symbol-keyed
  bindings, **`_promote_repeat_frame_bindings`** copies **`repeat_capture`**
  list-of-frame-dicts into **parallel list columns** for inner pattern variables,
  then **`expand_template_with_hygiene`**, then **`_finalize_macro_output`**
  (**`apply_layout`** + **`collect_binding_symbols`** +
  **`finalize_macro_hygiene_unbox_one_pass`**). The older one-shot
  **`apply_syntax_rules_hygiene`** helper still exists in **`hygiene`** for API
  parity / tests; **`SyntaxTransform`** does not call it.
* **`MacroTransform`** — handler **`(compiler, tc, **captures)`** then the
  same **`_finalize_macro_output`** pass on the handler's return value (no
  template expansion inside **`MacroTransform`**).
* **`SpecialTransform`** — handler return is the compile product (**no**
  automatic finalize/hygiene pass in **`syntax`** itself).
* **`special(form_symbol, *, patterns, unpack=None)`** — decorator that builds
  **`Syntax`** from patterns, each arm a **`SpecialTransform`**.
* **`parse_syntax_rules_pattern`** — lowers **RULE**-dialect syntax-rules
  patterns to **`matcher`** trees. **MATCH**-dialect rule surfaces use
  **`parse_explicit_rule_pattern`**, invoked from **`parse-match-pattern`** at
  runtime for **`compile/syntax-match`** emission paths that still call **`build-syntax`**.

Tests include `tests/syntax/test_syntax_bootstrap.py`,
`tests/syntax/test_syntax_builders.py`, **`tests/syntax/datum_syntax_helpers.py`**
(test-only row builders), **`tests/compile/specials/test_define_syntax.py`**
(integration), and **`tests/hygiene/test_hygiene.py`** (template ellipsis
and helpers).

### Migration policy (single stack)

The migration target is **one** post-cutover stack: **`matcher`**,
**`hygiene`**, and **`syntax`** as the compile-time dispatch and
pattern story. Delivery is intentionally a **single “Big Heave”**
replacement of the legacy matcher / hygiene / syntax-rules surface for this
codebase. **Running old and new matcher or hygiene stacks in parallel as a
long-term architecture is not a design goal**; incidental overlap while
landing patches is expected, but sustained dual-stack operation is not.

Complements the current architecture reference at
[unified-matcher-transformer-model.md](../language/unified-matcher-transformer-model.md),
which documents the implementation `matcher` is intended to supersede.

## As-built notes

These bullets record how the shipped tree behaves relative to the
high-level goals above; the rest of the document describes the same
system without a separate “planned vs landed” split.

* **Phase 4.5 (repeat frames and slot helpers).** Builder helpers
  `repeat_capture`, `slot_prefix`, `binding_inner_pattern`, and
  `BINDING_CELL`, plus a per-`ONEOF` capture-scope stack on
  `MatchCompiler`, support list-shaped `CAPTURE(REPEAT(...))` results.
  Native `REPEAT` carries `min_count` and threads trailing suffix width
  into inner attempts. A `REPEAT` whose operand is a slot-free `GROUP`
  yields one Python dict per successful iteration when that group’s
  inner pattern uses nested captures; unbound anonymous groups discard
  their scratch dict after the match. Tests live in
  `tests/matcher/test_repeat_frames.py` and `tests/native/test_matcher.c`.

* **Composites are composition-only.** `Composite._expand` returns a
  tree of primitives; hygienic composites may call `register_hygiene` on
  a `HygienicMatchCompiler` during lowering. `MatchCompiler` ignores those
  registrations. There is no post-match Python transform on the engine;
  `MatchResult.captures` is the payload. Legacy per-form result wrapper
  types are not part of the matcher surface.

* **Composite expansions (structural sketches).**

  | Composite  | Final shape (sketch)                                                                                                                             |
  |------------|--------------------------------------------------------------------------------------------------------------------------------------------------|
  | `DEFINE`   | `CAPTURE(slot, TYPE("symbol"))`                                                                                                                  |
  | `BINDING`  | `GROUP(slot, binding_inner_pattern())` — pair-backed subjects yield a `DatumFrame`; the whole binding cell spine is `DatumFrame.source` |
  | `BINDINGS` | `ONEOF(CAPTURE(slot, LITERAL(nil)), FORM(CAPTURE(slot, REPEAT(GROUP(None, ONEOF(...))))))` — nil arm first because `REPEAT` with `min_count` 0 always succeeds; each iteration runs a slot-free `GROUP` that produces one frame dict or `DatumFrame` |
  | `USING`    | `GROUP(slot, FORM(ONEOF(...)))` with four ordered `MATCH` branches: unpack, name+expr, expr-as-name, bare expr                                 |
  | `DECL`     | `GROUP(slot, ONEOF(TYPE("symbol"), FORM(MATCH(SYMBOL("name"))), FORM(MATCH(VALUE("type"), LITERAL_KW("as"), SYMBOL("name"))), FORM(MATCH(SYMBOL("name"), REST("decl_tail")))))` — inner `name`, `type`, `decl_tail` use `CAPTURE`/`REST` lowering; the matcher does not parse the keyword tail; pair-backed subjects carry the declaration cell on `DatumFrame.source` |
  | `FORMALS`  | `GROUP(slot, formals_inner_pattern())` — the outer slot binds a dict or `DatumFrame` (`positionals`, `defaults`, `star`, `kwonly`, `starstar`); the whole formals list spine is `DatumFrame.source` for pair-backed subjects. No bare-symbol formals arm |
  | `BODY`     | `REST(slot, min_tail=1)` (lowers to `CAPTURE(slot, REST(min_tail))`) plus a `scope_regions` hygiene entry                                       |

* **Hygiene is a separate data structure; the frozen matcher stays
  pure.** **Matching** and **thaw** use a strict **6-tuple**
  `("MATCHER", slots, externs, consts, nodes, entry)` with no hygiene
  fields and no separate version field in the wire tuple. Nothing
  hygiene-shaped is embedded in that constant or in `Matcher.from_tuple`.
  Optional binder/scope-region interpretation is collected at freeze time
  into a **`Hygiene`** instance (`koskari/hygiene.py`) with its own
  portable wire form (`Hygiene.to_tuple` / `from_tuple`, tag `HYGIENE`).
  Only **`HygienicMatchCompiler`** accumulates that plan via
  `build_hygiene()`; **`MatchCompiler`** produces no `Hygiene` object and
  does not need hygiene to compile or run the same rule program. Surface
  matcher syntax may still mention `scope:` and composites that register
  hygiene for **`HygienicMatchCompiler`**, but the baseline **`MatchCompiler`**
  path treats those aspects as inert for matching.

* **Improper-list FORMALS are rejected; matcher FORMALS is
  list-shaped only.** koskari does not allow improper lists as a
  formals shape. The `FORMALS` composite lowers to `GROUP`; there is
  no bare-symbol formals arm. Compile-side consumption of the GROUP
  payload lives in **`koskari.compile.helpers`** (`gather_formals`,
  `discover_docstring`, `helper_begin`, and related compile helpers).

* **`BINDINGS` matches one spine cell.** The composite is a `ONEOF`
  between `nil` and `FORM(CAPTURE(slot, REPEAT(GROUP(None, …))))`, so the
  binding list lives inside the subject element matched by that `FORM`,
  not as multiple consecutive top-level cells. A typical let-shaped rule
  uses `(MATCH(BINDINGS("bs"), BODY("body")))` so the first cell is the
  bindings list (or `nil`) and following cells are the body. The empty-list
  arm binds `captures["bs"]` to `nil` for that single cell.

* **Syntax expansion for `compile/define-syntax` is on `syntax`.**
  **`compile/specials/syntax.py`** lowers Koskari rule surfaces into quoted
  **`build-syntax`**, **`build-macro`**, and **`build-special`** call forms
  (with **`parse-rule-pattern`** / **`parse-match-pattern`**); evaluation yields
  **`syntax.Syntax`** transformers. **`koskari/importer/bootstrap.py`** registers
  those runtime builders for the standard library bootstrap.

* **Compound ellipsis and `repeat_capture`.** Lowered **`repeat_capture`**
  is **`CAPTURE` wrapping `REPEAT` of a slot-free `GROUP(None, …)`**
  around the inner row pattern. The native engine appends one Python value per
  successful iteration (dict copy for that `GROUP`, else sparse change
  dicts, else single-cell **`Datum`**, else a multi-cell slice tuple).
  **`captures[outer_slot]`** is therefore a bare **`list`**. Terminal
  symbol ellipsis in **`syntax`** rules uses **`repeat_capture(sym,
  TEST_ANY())`** so the list holds **`Datum`** per matched cell; when more
  pattern follows in the same row, **`REST(sym)`** is still used.
  **`expand_template_with_hygiene`** drives ellipsis from parallel Python
  lists (and **`_repeat_count`** treats **`tuple`** like **`list`** for
  length); **`syntax`** runs **`_promote_repeat_frame_bindings`** after a
  match for list-of-dict frames.

## Purpose

Koskari needs one matcher model that can serve several related uses:

1. **Syntax matching** for macros, syntax transformers, special-form dispatch, and source-form diagnostics.
2. **Runtime matching** for value matching, structured unpacking, and `match/case`-style branching.
3. **Hygiene extraction** for syntax expansion, where matched declarations, uses, and bodies define gensym substitution scopes.
4. **Composite matcher authoring** so reusable matchers such as `FORMALS`, `BINDING`, and `BINDINGS` can be written once and compiled into the same low-level matcher form.

The core idea is that user-facing matcher syntax, Python builder objects, frozen matcher constants, and thawed native matcher instances are different layers over the same underlying rule program.

---

## Layer Model

The full flow is:

```text
surface matcher syntax
  -> Python/object symbolic matcher layer
  -> canonical low-level tagged-tuple representation
  -> frozen code constant on disk / in bytecode
  -> thawed native Matcher instance
  -> runtime invocation with subject + extern bindings
  -> MatchResult
```

### 1. Surface matcher syntax

This is what users or compiler authors write in Koskari source. It reuses ordinary Koskari forms: symbols, quoted symbols, keywords, lists, and annotation syntax.

Examples:

```scheme
x
(x :: symbol)
(x val: 5)
(type: keyword)
(val: 5)
'x

(FORM (name :: symbol) ':: return_type (formals :: (FORMALS scope: body)) (body :: REST))

(data :: (CLASS Point x: 0 y))
```

### 2. Python/object symbolic matcher layer

This is the ergonomic implementation API used by compiler code and bootstrap definitions.

Examples:

```python
MATCH(
    SYMBOL("name"),
    LITERAL_KW("::"),
    VALUE("return_type"),
    FORMALS("formals", scope="body"),
    REST("body"),
)
```

Existing APIs such as `SYMBOL("name")`, `VALUE("expr")`, `REST("body")`, `BINDING(...)`, and `BINDINGS(...)` can remain as Pythonic constructors. They lower into the same symbolic matcher AST as the source syntax.

### 3. Canonical low-level tagged-tuple representation

This is the frozen form suitable for embedding into bytecode constants. It is canonical and compact (no version integer in the tuple). It should not contain Python builder objects.

Conceptually:

```text
("MATCHER",
  slots,
  externs,
  consts,
  nodes,
  entry)
```

Where:

* `slots` names capture slots (in emission order; duplicate display names are allowed at different capture sites when nested `GROUP` scopes separate them).
* `externs` names externally supplied values such as classes, types, predicates, or symbolic comparison values.
* `consts` stores embedded literal constants or sub-matcher constants.
* `nodes` is a bottom-up list of tagged nodes.
* `entry` is the root node index.

Hygiene plans, provenance, and other compile-time sidecars are **not**
fields of this tuple. They live in parallel Python values (notably
`Hygiene` from `hygiene`) when a caller uses
`HygienicMatchCompiler`.

### 4. Frozen code constant

The compiler emits bytecode/code constants containing the frozen matcher representation. Named composites are resolved and lowered before this point unless intentionally represented as externs.

A previously compiled function is not affected if a named matcher such as `FORMALS` is redefined later. The named matcher acted like a compile-time matcher macro that expanded to lower-level rules.

### 5. Thawed native Matcher instance

When code is realized into a callable function object, complex constants thaw. The frozen matcher tuple becomes a native `Matcher` instance containing compact C-native arrays and validation-derived metadata.

The runtime function refers to that matcher as a normal constant.

### 6. Invocation

A matcher invocation takes the subject plus extern bindings:

```python
m.match(obj, Point=Point, int=int, sentinel=sentinel)
```

Externs are explicit matcher parameters. The matcher may use them for:

* class matching: `(CLASS Point ...)`
* type checks: `(type: Point)` or `(x :: Point)`
* predicate checks
* symbolic value comparisons

The native matcher validates extern bindings at call entry, then matching uses indexed extern slots. Matching itself performs no namespace lookup.

---

## Syntax Proposal

The matcher syntax is a pattern language interpreted in matcher context. It does not require new reader syntax.

### Core pattern forms

```bnf
pattern ::= literal
          | capture
          | anonymous_constraint
          | composite
          | class_pattern
```

### Literals

```bnf
literal ::= number
          | string
          | keyword
          | quoted_symbol
```

Examples:

```scheme
5
"hello"
foo:
'x
```

A quoted symbol is a literal symbol. A bare symbol is a capture.

### Captures

```bnf
capture ::= symbol
          | "(" symbol capture_option* constraint* ")"
```

Examples:

```scheme
x
(x :: symbol)
(x type: int)
(x val: 5)
(x :: (FORM (type: keyword) (type: symbol)))
```

A bare symbol captures any value/form.

### Anonymous constraints

```bnf
anonymous_constraint ::= "(" constraint+ ")"
```

Examples:

```scheme
(type: symbol)
(:: int)
(val: 5)
(INT)
```

### Constraints

```bnf
constraint ::= "::" matcher_expr
             | "type:" matcher_expr
             | "val:" value_expr
             | matcher_expr
```

`::` and `type:` are equivalent as type/predicate/matcher constraints. `val:` is literal/value equality.

### Capture options

Capture options are compile-time matcher options, not matched input.

```bnf
capture_option ::= "scope:" symbol
                 | "mode:" symbol
                 | "role:" symbol
```

Examples:

```scheme
(formals :: (FORMALS scope: body))
(bindings :: (BINDINGS mode: parallel scope: body))
(body :: REST)
```

`scope: body` refers to another capture slot named `body`, not to an input form.

### Composite matchers

```bnf
composite ::= "(" matcher_head pattern* ")"
```

Core matcher heads:

```scheme
FORM
GROUP
MATCH
ONEOF
SEQ
REPEAT
OPTIONAL
REST
NOT
CLASS
```

Successful `MatchResult.captures` is a **sparse** dict: only slots that actually bound a value appear. There is no guarantee that every declared slot key exists with `ABSENT` — use containment checks or defaults where needed.

When the subject spine is backed by `KosPair` cells, values stored on capture paths (including `REST` slices and repeat frames) are wrapped in `koskari.lib.Datum` with `datum` and optional `position` (the tail cell’s position). Tuple/list subjects use `Datum` with `position` `None`. Callers read the captured value through the `.datum` attribute.

---

## Structural Matchers

### FORM

```scheme
(FORM inner)
```

Matches a nested source/value sequence: the subject must be one spine
element whose contents are matched by **`inner`** (often a **`MATCH`**
of several sequential pieces). In compile-time syntax matching, this
means a KosPair/proper list form. In runtime matching, this means a
sequence-like value according to the runtime subject adapter.

### MATCH / GROUP

```scheme
(MATCH p1 p2 ...)
(GROUP slot p1 p2 ...)
```

`MATCH` sequences several pattern elements on the current spine without
descending into a nested form. This is needed inside `ONEOF` and similar
places where each branch may contain multiple sequential elements.

In surface syntax, `GROUP` may name its first argument `slot`. The
builder lowers a named `GROUP` to **`CAPTURE`** wrapping a slot-free
**`GROUP`** of child patterns; inner captures nest into the group's
scratch dict, and **`CAPTURE`** commits that dict (or other sub-result)
into **`captures[slot]`**. An anonymous `GROUP` (no name / `None` in
Python) keeps the nested dict only while matching its children unless a
parent **`CAPTURE`** or **`REPEAT`** iteration collects it.

Example concept:

```scheme
(FORM
  a
  (ONEOF
    (MATCH b)
    (MATCH c d)
    (MATCH z (rest :: REST))))
```

This matches forms like:

```scheme
(a b)
(a c d)
(a z ...)
```

The `ONEOF` branches operate from the same originating cursor after `a`.

### ONEOF

```scheme
(ONEOF branch1 branch2 ...)
```

Tries alternatives in order. Each branch may be a single pattern node or a `MATCH` containing multiple nodes on the same spine.

Each `ONEOF` branch is independent for width analysis and REST allowance.

### SEQ

```scheme
(SEQ p)
```

Shorthand for a form containing repeated matches of `p`:

```scheme
(FORM (REPEAT p))
```

Useful for list-of patterns such as `BINDINGS`.

### REPEAT

```scheme
(REPEAT p1 p2 ...)
```

Consumes the current spine repeatedly. Each iteration matches the repeated branch and advances the cursor. Captures inside a named repeat produce a list of per-iteration sub-captures.

Invariant:

```text
REPEAT operand min_width must be >= 1.
```

This forbids zero-width loops such as:

```scheme
(REPEAT (OPTIONAL p))
```

which should be expressed as `REST` if arbitrary suffix capture is intended.

### OPTIONAL

```scheme
(OPTIONAL p)
```

Greedy maybe-match. It succeeds whether `p` matches or not. If absent, captures inside it receive the defined absent/default sentinel behavior.

### REST

```scheme
(REST)
(rest :: REST)
```

On the wire, **`REST`** is **`("REST", min_tail)`** where **`min_tail`**
is the minimum number of spine elements this rest must consume (often `0`
for a greedy remainder, or `1` for body tails). Naming a rest in the
surface language lowers to **`CAPTURE(slot, REST(min_tail))`**; the
**`REST`** tuple itself never carries a slot index.

Consumes the remaining tail with suffix reservation when nonterminal.

Terminal REST captures the remaining tail as-is (subject to `min_tail`).

Nonterminal REST reserves enough suffix width for following patterns. For example:

```scheme
(FORM a (r :: REST) w x)
```

captures into `r` while preserving enough elements for `w x` to match.

Within a single `FORM` branch, only one REST is allowed. Each `ONEOF` branch is its own branch for this constraint.

For width computation, REST is treated specially as the unique variable-tail element for the branch. Operationally it is min zero and unbounded, but branch validation enforces only one variable REST per `FORM` branch.

### NOT

```scheme
(NOT p)
```

Negative check. It must not consume input. It succeeds if `p` does not match at the same cursor.

### CLASS

```scheme
(CLASS Point x: 0 y)
(data :: (CLASS Point x: 0 y))
```

Matches a Python-style class pattern. It checks the subject against an externally supplied class/type and then extracts fields via `__match_args__`, attributes, or keyword field rules.

Explicit `CLASS` avoids ambiguity with named composite matchers and ordinary type constraints.

Optional sugar may allow:

```scheme
(data :: (Point x: 0 y))
```

but only if resolution rules make it unambiguous. The safer canonical form is explicit `CLASS`.

---

## Hygiene Integration

Native **`MatchResult`** carries **captures only** (plus match/miss).
Hygiene is **not** attached to the result object from the engine.
Callers that need expansion or binding-symbol discovery pair the same
successful captures with a **`Hygiene`** plan built alongside the
pattern (via `HygienicMatchCompiler.build_hygiene()`) and drive walks
through `koskari/hygiene` (`introduced_symbols_from_match`,
`scope_region_targets`, `collect_binding_symbols`, etc.). That keeps
**matching pure** while still allowing matcher syntax to carry optional
`scope:` / composite hints for authors who compile with the hygienic
compiler.

Concept mapping:

* **`DEFINE`**, **`DECL`**, **`BINDING`**, **`BINDINGS`**, **`FORMALS`**, **`USING`**: composites that contribute **binder** semantics in the `Hygiene` plan when registered.
* **`BODY`**: registers a **scope-region** slot (body extent) in the plan; at match time it lowers to **`REST(slot, min_tail=1)`**, i.e. **`CAPTURE(slot, REST(1))`**, on the wire.
* **`scope:`** on composites: recorded in wire rows as the tuple of scope-region slot names the binder applies over; interpretation is in `hygiene`, not in the frozen 6-tuple.
* **`CAPTURE`**: may carry `hygiene_role` / `scope_slot` fields in the node encoding for a future migration; they are not a substitute for the separate `Hygiene` object today.

The low-level matcher does not need separate primitive kinds for every hygiene-aware surface form. Composites lower into ordinary capture/test/group structure; hygiene **interpretation** lives on the composite instances referenced from `Hygiene.binders` and `Hygiene.scope_regions`.

Example:

```scheme
(formals :: (FORMALS scope: body))
(body :: REST)
```

A successful match binds `formals` and `body` in **`captures`**. If
this pattern was frozen with **`HygienicMatchCompiler`**, the companion
**`Hygiene`** plan records that `FORMALS` introduces binders scoped over
the `body` capture slot; **`MatchCompiler`** alone yields the same
native matcher without that plan.

---

## Current Matcher Examples Rewritten

### Lambda

Current Python-style patterns:

```python
MATCH(SYMBOL("name"), LITERAL_KW("::"), VALUE("return_type"), FORMALS("formals", scope="body"), REST("body"))
MATCH(SYMBOL("name"), LITERAL_KW("type:"), VALUE("return_type"), FORMALS("formals", scope="body"), REST("body"))
MATCH(LITERAL_KW("::"), VALUE("return_type"), FORMALS("formals", scope="body"), REST("body"))
MATCH(LITERAL_KW("type:"), VALUE("return_type"), FORMALS("formals", scope="body"), REST("body"))
MATCH(SYMBOL("name"), FORMALS("formals", scope="body"), REST("body"))
MATCH(FORMALS("formals", scope="body"), REST("body"))
```

New matcher syntax:

```scheme
(ONEOF
  (GROUP (name :: symbol) ':: return_type (formals :: (FORMALS scope: body)) (body :: REST))
  (GROUP (name :: symbol) type: return_type (formals :: (FORMALS scope: body)) (body :: REST))
  (GROUP ':: return_type (formals :: (FORMALS scope: body)) (body :: REST))
  (GROUP type: return_type (formals :: (FORMALS scope: body)) (body :: REST))
  (GROUP (name :: symbol) (formals :: (FORMALS scope: body)) (body :: REST))
  (GROUP (formals :: (FORMALS scope: body)) (body :: REST)))
```

When this is the whole form rule for `compile/lambda`, it may be wrapped in `FORM` depending on whether the matcher is invoked on the interior of the form or the whole source form.

Important: `name` must be `(name :: symbol)`. Bare `name` would allow any form.

### Let

Current Python-style patterns:

```python
MATCH(SYMBOL("name"), BINDINGS("bindings", mode="parallel", scope="body"), REST("body"))
MATCH(BINDINGS("bindings", mode="parallel", scope="body"), REST("body"))
```

New matcher syntax:

```scheme
(ONEOF
  (GROUP (name :: symbol) (bindings :: (BINDINGS mode: parallel scope: body)) (body :: REST))
  (GROUP (bindings :: (BINDINGS mode: parallel scope: body)) (body :: REST)))
```

### Letrec

```scheme
(GROUP (bindings :: (BINDINGS mode: parallel scope: body)) (body :: REST))
```

### Binding

Current `BINDING`:

```python
_BINDING_FORMS = [
    LIST(VALUE("expr"), LITERAL_KW("as"), SYMBOL("name")),
    LIST(SYMBOL("name"), VALUE("expr")),
]
```

New composite matcher:

```scheme
(define-matcher BINDING
  (ONEOF
    (FORM expr 'as (name :: symbol))
    (FORM (name :: symbol) expr)))
```

`BINDINGS` is a sequence of repeated `BINDING` records:

```scheme
(define-matcher BINDINGS
  (SEQ (binding :: BINDING)))
```

Or with an outer capture at use site:

```scheme
(bindings :: BINDINGS)
```

### Formals Sketch

```scheme
(define-matcher FORMALS
  (FORM
    (pos :: (REPEAT (FORM (val :: (NOT (type: keyword))))))
    (kwds :: (REPEAT (FORM (key :: keyword) (val :: (NOT (type: keyword))))))))
```

The intended result shape:

```text
{
  pos:  [{val: ...}, ...],
  kwds: [{key: foo:, val: ...}, {key: bar:, val: ...}]
}
```

Named `REPEAT` captures produce lists of per-iteration records, stored in the slot as a bare Python list (not `Datum`-wrapped). This is the main case where nested capture result shaping matters.

---

## Runtime Matching vs Syntax Matching

The rules are shared, but subject adapters differ.

### Runtime matching

Runtime matching operates over Python values. Sequence matching should use a `PySequence_Fast`-style adapter for tuple/list coercion, not KosPair traversal.

Use cases:

* `match/case` over runtime values
* structured unpacking
* runtime class matching
* runtime value/type/predicate constraints

Runtime matchers receive the object and extern bindings:

```python
matcher.match(obj, Point=Point, symbol=Symbol)
```

The result can then be applied to a namespace as a separate operation.

### Syntax matching

Syntax matching operates over source forms, typically KosPair cons lists. It should use a KosPair subject adapter.

Use cases:

* macros
* syntax transformers
* special-form dispatch
* source-form diagnostics
* hygiene extraction

The core matcher API only determines match/no-match and fills
**`captures`**. Hygiene and provenance are layered by the caller using
a separate `Hygiene` plan and (eventually) other side channels, not by
extending **`MatchResult`**.

### Shared implementation through subject adapters

There should be one native matcher implementation with different subject adapters:

```c
typedef struct {
    Py_ssize_t (*len)(KosMatchSubject *subject);
    PyObject *(*get)(KosMatchSubject *subject, Py_ssize_t index);
    PyObject *(*tail)(KosMatchSubject *subject, Py_ssize_t start);
    int (*as_form)(PyObject *obj, KosMatchSubject *out);
} KosMatchSubjectVTable;
```

Runtime sequence adapter:

* Uses `PySequence_Fast`-style conversion.
* Provides O(1) length and index.
* Tail materialization returns the appropriate runtime sequence or view.

Syntax/KosPair adapter:

* Walks the cons spine.
* May cache length/index cells per invocation.
* Preserves source-form identity for diagnostics and hygiene.
* Handles proper/improper list policy explicitly.

The matcher nodes do not care which adapter is used.

---

## Match Results

The public match result is binary at the lowest level:

```text
matched / not matched
```

A non-matching value is not an error.

Exceptions are reserved for invalid matcher state, invalid invocation environment, failed validation, missing externs, or internal bugs.

On match, the native result contains:

* capture slots
* nested capture records
* repeated capture lists
* optional absent/default sentinels

Optional **provenance** or **diagnostic** data could be added later
without conflating them with hygiene. **Hygiene graphs** are not part
of `MatchResult`; they live in **`Hygiene`** and are applied using the
same **`captures`** mapping after a successful match.

Applying captures to a namespace is a later, separate operation.
Matcher inspection, dispatch, and unpacking use **`MatchResult`**;
hygiene walks use **`captures`** plus **`Hygiene`**.

---

## Width Model

Every node computes at least:

```text
min_width
max_width
```

`min_width == 0` covers the `can_empty` concern.

Width is measured in elements consumed from the current spine.

Important cases:

* Literal/test/capture of one element: min=1, max=1.
* `MATCH` and `GROUP`: sum of child widths along the current spine.
* `FORM`: consumes one element from the outer spine, then matches an inner subject.
* `ONEOF`: min is min(branch.min); max is max(branch.max), with unbounded if any branch is unbounded.
* `OPTIONAL`: min=0, max=child.max.
* `REPEAT`: min=0, max=unbounded, but child min must be >= 1.
* `REST`: unique branch-level variable tail; min=0 and operationally unbounded, with suffix reservation.
* `NOT`: min=0, max=0; must not consume.

Branch validation:

* No recursive matcher definitions.
* No more than one REST per `FORM` branch.
* Each `ONEOF` branch is its own branch for REST allowance.
* `REPEAT` operand must consume at least one element.
* Subnode indices must be acyclic.

---

## Native C Representation

The native matcher is a flexible array of `KosMatchNode` structs
(`koskari/lib/matcher.h`). Each node stores `kind` (`KOS_MATCH_NODE_*`),
`flags`, `next` (sibling link for `MATCH`/`GROUP` children, next `ONEOF`
branch, or next `FIELD` in a `CLASS` chain; otherwise `KOS_MATCH_NO_INDEX`),
`min_width`, `max_width`, and a `union` payload selected by `kind`.

Validation requires acyclic references and bottom-up emission order so
child indices precede their parents.

Sketch (field names match the shipped header; `KosMatchMatch` is the
payload for `KOS_MATCH_NODE_MATCH`):

```c
typedef enum {
  KOS_MATCH_NODE_TEST = 0,
  KOS_MATCH_NODE_CAPTURE,
  KOS_MATCH_NODE_FORM,
  KOS_MATCH_NODE_MATCH,
  KOS_MATCH_NODE_GROUP,
  KOS_MATCH_NODE_ONEOF,
  KOS_MATCH_NODE_REPEAT,
  KOS_MATCH_NODE_OPTIONAL,
  KOS_MATCH_NODE_REST,
  KOS_MATCH_NODE_NOT,
  KOS_MATCH_NODE_CLASS,
  KOS_MATCH_NODE_FIELD,
} KosMatchNodeKind;

typedef struct {
  uint16_t kind;
  uint16_t flags;
  uint32_t next;
  uint32_t min_width;
  uint32_t max_width;
  union {
    KosMatchTest     test;
    KosMatchCapture  capture;
    KosMatchForm     form;
    KosMatchMatch     match;
    KosMatchGroup    group;
    KosMatchOneof    oneof;
    KosMatchRepeat   repeat;
    KosMatchOptional optional;
    KosMatchRest     rest;
    KosMatchNot      not_;
    KosMatchClass    class_;
    KosMatchField    field;
  } as;
} KosMatchNode;
```

Payload highlights:

* **`KosMatchTest`**: `test_kind` (`KOS_MATCH_TEST_*`), `ref_index` into
  `Matcher.consts` (literal object, extern name index for `TYPE`/`PREDICATE`,
  etc.).
* **`KosMatchCapture`**: `slot` index into `Matcher.slots`, `subnode`
  (`KOS_MATCH_NO_INDEX` when capturing a single spine element with no inner
  pattern), `hygiene_role` (`uint16_t`), `scope_slot` index or
  `KOS_MATCH_NO_INDEX`.
* **`KosMatchForm`**: inner `subnode`.
* **`KosMatchMatch`** / **`KosMatchGroup`**: `first_child`; further children
  chain through each child node's `next` field.
* **`KosMatchOneof`**: `first_branch`; branches chain through `next`.
* **`KosMatchRepeat`**: inner `subnode`, `min_count` (minimum successful
  iterations; the Python builder always emits this on the wire tuple).
* **`KosMatchOptional`**: inner `subnode`.
* **`KosMatchRest`**: `min_tail` only — no slot field; named rests are
  implemented as **`CAPTURE`** whose `subnode` is a **`REST`** node.
* **`KosMatchNot`**: inner `subnode`.
* **`KosMatchClass`**: `type_extern`, `first_field` (positional and keyword
  fields form a `FIELD` chain via `next`).
* **`KosMatchField`**: `field_kind`, `subnode`, `name_const` (keyword attrs).

### Primitive node policy

Use distinct node kinds only when cursor behavior differs.

Likely primitive node kinds:

* `TEST`
* `CAPTURE`
* `FORM`
* `GROUP` / `MATCH`
* `ONEOF`
* `REST`
* `REPEAT`
* `OPTIONAL`
* `NOT`
* `CLASS`

Do not make every surface matcher a native primitive. Examples:

* `SYMBOL(name)` lowers to `CAPTURE` with a `TEST`/`TYPE` subnode (or
  shorthand that freezes to the same shape).
* `DECL(name)` lowers to a composite `CAPTURE` over declaration shapes
  (see the composite sketches in **As-built notes**); optional
  `hygiene_role` on `CAPTURE` is reserved and is not the primary hygiene
  mechanism today.
* `VALUE(name)` lowers to unconstrained capture.
* `LITERAL 5` lowers to `TEST_LITERAL 5`.
* `(x val: 5)` lowers to `CAPTURE slot=x` wrapping or carrying `TEST_LITERAL 5`.
* `BINDING` lowers to a composite `ONEOF` over `FORM` branches.
* `BINDINGS` lowers to repeated `BINDING` records.

---

## Tagged Tuple Frozen Representation

The top-level constant is always a **6-tuple**:

```text
("MATCHER", slots, externs, consts, nodes, entry)
```

Each element of **`nodes`** is a Python tuple whose leading string is the
node tag. Arities and field meanings are enforced in **`thaw_node`** in
`koskari/lib/matcher.c`.

| Tag         | Tuple shape |
|-------------|-------------|
| `TEST`      | `("TEST", test_kind, ref_index)` — `test_kind` is one of `ANY`, `LITERAL`, `TYPE`, `PREDICATE`; `ref_index` selects a `consts` entry or extern slot as appropriate for that kind |
| `CAPTURE`   | `("CAPTURE", slot_index, subnode_index, hygiene_role, scope_slot_index)` — indices into `slots` use the same integers stored on `Matcher.slots`; `subnode_index` is `-1` when there is no inner pattern (single-element capture) |
| `FORM`      | `("FORM", subnode_index)` |
| `MATCH`     | `("MATCH", child0, child1, …)` |
| `GROUP`     | `("GROUP", child0, child1, …)` — child indices only; no embedded capture slot |
| `ONEOF`     | `("ONEOF", branch0, branch1, …)` |
| `REPEAT`    | `("REPEAT", subnode_index)` or `("REPEAT", subnode_index, min_count)` — Python lowering always emits the three-element form |
| `OPTIONAL`  | `("OPTIONAL", subnode_index)` |
| `REST`      | `("REST", min_tail)` — no slot field; a named rest is lowered to `CAPTURE` → `REST` |
| `NOT`       | `("NOT", subnode_index)` |
| `CLASS`     | `("CLASS", type_extern_index, field0, field1, …)` |
| `FIELD_POS` | `("FIELD_POS", subnode_index)` |
| `FIELD_KW`  | `("FIELD_KW", subnode_index, name)` — `name` is a `str` interned into `consts` |

Named composites (**`FORMALS`**, **`BINDINGS`**, etc.) expand to these
primitives before freeze; there is no `CALL_MATCHER` tag.

### Sketch: lambda-shaped clause

Surface:

```scheme
(GROUP (name :: symbol) ':: return_type (formals :: (FORMALS scope: body)) (body :: REST))
```

After lowering, outer names become **`CAPTURE`** rows; nested structure uses
slot-free **`GROUP`** / **`MATCH`**; **`REST("body")`** lowers to a
**`CAPTURE`** row whose **`subnode`** indexes a **`("REST", min_tail)`**
node (typically `min_tail` is `0` for a greedy remainder inside a larger
**`MATCH`**); **`FORMALS`** inlines to its own subgraph of
**`FORM`/`MATCH`/`REPEAT`/`GROUP(None, …)`** nodes (see **`FORMALS`** in
`matcher.py`), not a pointer to a separate frozen matcher constant.

A **`Hygiene`** value built through **`HygienicMatchCompiler`** (not part of
the six-tuple) records binder edges such as **`FORMALS`** scoped over the
**`body`** slot.

### Sketch: class pattern

Surface:

```scheme
(data :: (CLASS Point x: 0 y))
```

Lowers to **`FIELD_POS`** / **`FIELD_KW`** chains under **`CLASS`**, with an
outer **`CAPTURE`** for **`data`** referencing the **`CLASS`** node.

Invocation:

```python
matcher.match(obj, Point=Point)
```

---

## Extern Binding ABI

Extern bindings are accepted through the ordinary Python call API initially:

```python
matcher.match(obj, Point=Point, int=int, sentinel=sentinel)
```

Later this can be optimized with vectorcall or prebuilt extern environments.

At call entry:

1. Validate that all required externs are present.
2. Validate kind/signature where required.
3. Populate a compact extern slot frame.
4. Run the matcher using extern indices.

This avoids module-relative thaw complexity while still making class/type/value symbolic references explicit and available to low-level matcher execution.

---

## Dynchain / Runtime Match Case Strategy

Runtime `match/case` should avoid repeatedly converting the same subject for each case.

Likely strategy:

1. Evaluate the match subject once.
2. Push or establish a match-case dynnode/state object.
3. Convert/adapt the subject once for the relevant matching mode.
4. Walk case matchers in order.
5. On match, apply captures to a namespace as a separate operation.
6. Jump to the selected handler body.

The dynnode/state object can hold:

* original subject
* adapted subject
* current case index
* capture/result frame
* cleanup/unwind information for applied namespace bindings

Syntax matching for special forms may use a similar internal loop, but over KosPair source forms and without runtime dynchain concerns unless needed by compilation flow.

---

## Error and Failure Policy

Matcher failure is normal:

```text
not matched
```

A subject value that does not satisfy a pattern is never an error.

Exceptions are reserved for:

* invalid matcher structure
* invalid frozen representation
* missing extern binding
* invalid extern binding kind
* internal matcher bug
* validation failure that should have been caught earlier

Creation/thaw validation should be strict so runtime matching does not discover structural matcher errors.

For diagnostics, callers may request provenance or best-failure tracing, but this is optional. The core matching API can simply say no-match.

---

## Self-Describing Matcher Syntax

The surface matcher syntax defined in "Syntax Proposal" is itself an
ordinary koskari s-expression form. The matcher primitives can
therefore describe its shape: a single matcher pattern recognises one
matcher-syntax expression element and binds the relevant pieces into
captures, and a small walker re-applies the matcher recursively to
lower the captured pieces into a frozen tuple. The walker is the only
place that knows the head-symbol vocabulary and the keyword-tag
semantics; the matcher itself is pure shape recognition.

This is the shape parser implemented as **`META_PATTERN`** /
**`META_MATCHER`** / **`parse_matcher`** in Phase 6. It is also the
canonical demonstration of the discipline in **As-built notes**: the
matcher describes the BNF, the walker enforces the semantics.
**`define-matcher`** and related specials can lower surface matcher
syntax through this stack into a frozen tuple for **`Matcher.from_tuple`**.

### Surface element classification

Every matcher pattern element on the spine is exactly one of the
shapes below. The "Head atom" column is the recognising key the
meta-matcher dispatches on.

| Surface form              | Head atom                  | Subject shape            |
|---------------------------|----------------------------|--------------------------|
| `x`                       | (none, atom)               | bare symbol element      |
| `5` `"hello"` `foo:`      | (none, atom)               | non-symbol non-pair atom |
| `'sym`                    | symbol `quote`             | pair `(quote sym)`       |
| `(:: pat...)`             | keyword `::`               | pair                     |
| `(type: pat...)`          | keyword `type:`            | pair                     |
| `(val: expr)`             | keyword `val:`             | pair                     |
| `(CLASS Sym fields...)`   | symbol `CLASS`             | pair                     |
| `(HEAD args...)`          | symbol (any other)         | pair                     |

The last row covers two surface forms that share a shape: the
**named capture** (`(name :: symbol)`,
`(formals :: (FORMALS scope: body))`) and the **composite invocation**
(`(FORM ...)`, `(ONEOF ...)`, `(BINDINGS mode: parallel ...)`). The
matcher does not try to distinguish these — that is a one-line
dispatch in the walker against a registered set of matcher-head
symbols.

### Meta-matcher

In matcher Python builder form, the meta-matcher is a single
ordered `ONEOF` whose branches correspond row-for-row to the table
above:

```python
META_PATTERN = ONEOF(
    # 1. quoted symbol literal: (quote SYM)
    FORM(GROUP(
        LITERAL_SYM("quote"),
        CAPTURE("quoted_symbol", TYPE("symbol")))),

    # 2. anonymous :: constraint: (:: PAT...)
    FORM(GROUP(
        LITERAL_KW("::"),
        REST("dcolon_args"))),

    # 3. anonymous type: constraint: (type: PAT...)
    FORM(GROUP(
        LITERAL_KW("type"),
        REST("type_args"))),

    # 4. anonymous val: constraint: (val: EXPR)
    FORM(GROUP(
        LITERAL_KW("val"),
        CAPTURE("val_expr"))),

    # 5. CLASS pattern: (CLASS Sym FIELDS...)
    FORM(GROUP(
        LITERAL_SYM("CLASS"),
        CAPTURE("class_extern", TYPE("symbol")),
        REST("class_fields"))),

    # 6. general (HEAD ARGS...) -- composite invocation OR named capture
    FORM(GROUP(
        CAPTURE("head", TYPE("symbol")),
        REST("head_args"))),

    # 7. bare symbol -- captures any value into a slot named after itself
    CAPTURE("bare_symbol", TYPE("symbol")),

    # 8. atomic literal -- number, string, keyword, etc.
    CAPTURE("atom_literal"),
)
```

The pattern is invoked against a length-1 subject containing one
matcher-syntax element:

```python
META_MATCHER = make_matcher(MATCH(META_PATTERN))
result = META_MATCHER.match((elem,), symbol=symbol,
                            keyword=keyword)
```

At run time the native matcher validates and fills **extern** slots declared
during lower; KosPair-style compile subjects typically rely on the engine's
default **`symbol`**, **`keyword`**, **`pair`**, **`cons`**, and **`nil`**
bindings when callers pass only the subject (as **`syntax`** transform
arms do today).

The branch order matters:

1. `(quote SYM)` precedes the general `(HEAD ARGS...)` arm so a
   leading `quote` is recognised as a literal-symbol marker rather
   than as a capture named `quote`.
2. The three anonymous-constraint arms (`::`, `type:`, `val:`)
   precede the general arm so their leading keyword is consumed
   structurally rather than folded into a capture's option list.
3. `CLASS` is split out so the class extern's symbol is bound to a
   distinct slot — the walker treats `(CLASS ...)` differently from
   any other named composite.
4. The general `(HEAD ARGS...)` arm sits at the end of the list-form
   arms; its head may be either a known matcher head
   (`FORM`, `GROUP`, `ONEOF`, `REPEAT`, `OPTIONAL`, `REST`, `NOT`,
   `FORMALS`, `BINDING`, `BINDINGS`, `USING`, `DEFINE`, `DECL`, ...)
   or a user-chosen capture name. The walker decides.
5. The two atom arms come last, in the order bare-symbol then
   atomic-literal, because `bare_symbol` constrains to `symbol` and
   `atom_literal` is unconstrained.

### Capture-shape contract

After a successful match the captures dict has exactly one of the
following slot configurations bound; all other slots are `ABSENT`:

| Shape recognised        | Slots bound                          |
|-------------------------|--------------------------------------|
| `'sym`                  | `quoted_symbol`                      |
| `(:: pat...)`           | `dcolon_args`                        |
| `(type: pat...)`        | `type_args`                          |
| `(val: expr)`           | `val_expr`                           |
| `(CLASS Sym fields...)` | `class_extern`, `class_fields`       |
| `(HEAD args...)`        | `head`, `head_args`                  |
| `x`                     | `bare_symbol`                        |
| `5`                     | `atom_literal`                       |

The walker dispatches on which slot is non-`ABSENT`; the captured
tail (`*_args`, `class_fields`, `head_args`) is the *raw* koskari
list whose elements the walker re-feeds into `META_MATCHER`.

### Walker outline

```python
KNOWN_HEADS = {
    "FORM", "GROUP", "MATCH", "ONEOF", "SEQ",
    "REPEAT", "OPTIONAL", "REST", "NOT",
    "FORMALS", "BINDING", "BINDINGS", "USING",
    "DEFINE", "DECL",
}

CAPTURE_OPTION_KEYS = {"scope", "mode", "role"}


def lower(elem):
    """
    Lower one matcher-syntax element to a matcher pattern object.
    """

    r = META_MATCHER.match((elem,), symbol=symbol,
                           keyword=keyword)
    if not r.matched:
        raise SpecialError(
            "not a valid matcher pattern: %r" % (elem,), elem)
    c = r.captures

    if c["bare_symbol"] is not ABSENT:
        return CAPTURE(str(c["bare_symbol"]))

    if c["atom_literal"] is not ABSENT:
        return LITERAL(c["atom_literal"])

    if c["quoted_symbol"] is not ABSENT:
        return LITERAL_SYM(str(c["quoted_symbol"]))

    if c["dcolon_args"] is not ABSENT:
        return _lower_anon_constraint("::", c["dcolon_args"])

    if c["type_args"] is not ABSENT:
        return _lower_anon_constraint("type:", c["type_args"])

    if c["val_expr"] is not ABSENT:
        return LITERAL(c["val_expr"])

    if c["class_extern"] is not ABSENT:
        return _lower_class(c["class_extern"], c["class_fields"])

    if c["head"] is not ABSENT:
        head = c["head"]
        args = c["head_args"]
        if str(head) in KNOWN_HEADS:
            return _lower_composite(head, args)
        return _lower_named_capture(head, args)

    raise SpecialError(
        "internal: meta-matcher matched with no slot bound", elem)
```

`_lower_named_capture` walks `args`, splitting it into capture
options (keyword-value pairs whose keyword name is in
`CAPTURE_OPTION_KEYS`) and constraints (everything else, each itself
re-fed to the meta-matcher). `_lower_composite` recurses by mapping
each element of `args` through `lower` and assembling the result via
the named composite's Python constructor (or directly via frozen
tuple, depending on the registry).

### One-level recursion via walker

matcher has no native pattern-recursion primitive; a matcher
cannot reference itself by name from inside its own definition. The
meta-matcher therefore matches **one level at a time**, and the
walker is the loop driver. Each branch's `head_args` /
`class_fields` / `dcolon_args` capture is the raw tail of the source
list; the walker iterates that tail and calls `lower` on each
element.

This is acceptable for a ground-up bootstrap because matcher syntax
has no left recursion and no productions with shared cursor state
across recursion: every recursive position is **inside a
parenthesised form** that the meta-matcher already isolates as a
captured sub-list. Lowering is therefore strictly bottom-up with no
risk of left-recursive non-termination.

### Worked example

Take a clause from a hypothetical `define-matcher` for `BINDING`:

```scheme
(FORM (name :: symbol) expr)
```

The walker sees `(FORM ...)` as one matcher pattern element. The
meta-matcher binds:

```text
captures = {
    head:        symbol("FORM"),
    head_args:   ((name :: symbol) expr),
    ...other slots ABSENT...
}
```

`str(head) == "FORM"` is in `KNOWN_HEADS`, so `_lower_composite`
runs. It iterates `head_args` and calls `lower` on each element:

* `(name :: symbol)` re-enters the meta-matcher and matches the
  general arm with `head = symbol("name")` and
  `head_args = (:: symbol)`. `name` is not in `KNOWN_HEADS`, so
  `_lower_named_capture` is called. It walks `head_args` looking
  for capture options (none) and constraints (one `::` constraint
  whose payload is the symbol `symbol`), and returns
  `CAPTURE("name", TYPE("symbol"))`.

* `expr` re-enters the meta-matcher and matches the bare-symbol
  arm, returning `CAPTURE("expr")`.

`_lower_composite` assembles the parent:
`FORM(MATCH(CAPTURE("name", TYPE("symbol")), CAPTURE("expr")))`.

### Why this matters

Once the meta-matcher exists, the matcher language describes itself.
The same `MatchResult` shape that `helper_bindings` walks is what
`define-matcher` walks; the same discipline that makes
`gather_formals_ng` thin makes `define-matcher` thin. Every parser
in the koskari compiler that consumes a matcher pattern reduces to
**one matcher constant plus one walker**, and the matcher constant
is itself a frozen tuple that can be inspected, pretty-printed, or
shipped as a stable wire format.

This is the closing of the bootstrap: the matcher engine is
responsible for shape recognition and capture binding only; every
parser, every matcher constructor, every special-form dispatcher is
one matcher plus one walker over its captures dict.

---

## Implementation Strategy: Low to High

### Phase 1 — Native low-level matcher core *(landed)*

C-native tagged node array, validator, and matcher core in
`koskari/lib/matcher.{c,h}`.

Primitive node kinds present: `TEST`, `CAPTURE`, `FORM`, `MATCH`,
`GROUP`, `ONEOF`, `REST`, `REPEAT`, `OPTIONAL`, `NOT`, `CLASS`, `FIELD`.

Implemented:

* bottom-up node validation
* child index acyclicity
* width computation (`min_width`, `max_width`, `KOS_MATCH_UNBOUNDED`)
* one REST per FORM branch (with `OPTIONAL` rest-bearing fold-in)
* REPEAT min-width validation (operand min must be `>= 1`)
* capture slot validation
* extern table validation
* `NOT` rejects subtrees containing `CAPTURE`

### Phase 2 — Subject adapters *(landed)*

Adapters for both `PySequence_Fast` (tuple/list runtime matching)
and `KosPair` (compile-time source-form matching) live in
`koskari/lib/matcher.c`. The matcher core is adapter-agnostic;
`subject_from_any` dispatches at the subject root and `match_form`
re-enters the dispatch when descending into a nested element.

### Phase 3 — Frozen tuple schema and thaw *(landed)*

Schema is the **six-element** tuple
`("MATCHER", slots, externs, consts, nodes, entry)` (no separate version
integer in the wire tuple). `Matcher.from_tuple` performs structural and
width validation; the resulting object is a real native `Matcher` held by
ordinary Python code-constant machinery. `thaw` in `koskari/matcher.py`
wraps `Matcher.from_tuple` for symmetry with the builder API. **Hygiene** is
not a field of this tuple; see **As-built notes** (hygiene split).

### Phase 4 — Python builder API *(landed)*

Builder classes and shorthand factories live in
`koskari/matcher.py`:

```python
TEST_ANY, LITERAL, TYPE, PREDICATE, CAPTURE,
FORM, GROUP, ONEOF, REPEAT, OPTIONAL, REST, NOT, CLASS, FIELD,
BIND, VALUE, SYMBOL, PAIR, LITERAL_KW, LITERAL_SYM, LIST,
make_matcher, thaw,
```

All ergonomic shorthands lower into the same frozen-tuple
representation as the primitives.

### Phase 4.5 — Repeat frames and slots helpers *(landed)*

Builder helpers in `koskari/matcher.py`:

```python
repeat_capture(slot, inner)       # CAPTURE(slot, REPEAT(GROUP(None, inner)))
slot_prefix(pattern, prefix)      # rename CAPTURE/REST slots
binding_inner_pattern()           # FORM(ONEOF(...)) for one cell
BINDING_CELL(slot)                # slot_prefix(binding_inner_pattern, slot)
```

`MatchCompiler` uses a `capture_scope_stack` so each `ONEOF` branch
is its own slot-uniqueness scope; identical slot names across
mutually exclusive arms are valid.

### Phase 5 — Bootstrap composites *(landed, cleanroom rewrite)*

Composites in `koskari/matcher.py`: `DEFINE`, `DECL`, `BINDING`,
`BINDINGS`, `FORMALS`, `USING`, `BODY`. Each composite is a thin
factory: `_expand` returns a tree of primitives. During lowering,
hygienic composites implement **`register_hygiene`** so a
**`HygienicMatchCompiler`** can assemble a **`Hygiene`** plan; a
baseline **`MatchCompiler`** ignores that hook. There is no post-match
Python transform; the captures dict is the **`MatchResult`** payload.
Composite sketches are summarized under **As-built notes** above and in
**Current Matcher Examples Rewritten** below.

### Phase 6 — Matcher syntax parser *(landed)*

The matcher-syntax parser lives in `koskari/matcher.py` and uses
the existing primitives/composites to unpack matcher syntax itself.
Implemented pieces:

* `META_PATTERN` (shape classifier for one syntax element)
* `META_MATCHER` (native matcher built from `META_PATTERN`)
* `parse_matcher(elem)` (walker/lowerer from syntax element to
  `Pattern`)
* registry surface: `MATCHER_REGISTRY`, `register_matcher`,
  `lookup_matcher`, and built-in head factory registration
* parser test suite: `tests/matcher/test_syntax.py`

The shape and capture contract of that parser are sketched in the
"Self-Describing Matcher Syntax" section above. With this landed,
source-level matcher authoring becomes:

```scheme
(define-matcher BINDING
  (ONEOF
    (FORM expr 'as (name :: symbol))
    (FORM (name :: symbol) expr)))
```

The matcher language now describes itself at the parser layer.
`define-matcher` as a first-class **special** that lowers through this parser
into installed matcher constants is still **not** the primary authoring path
for production specials today (most patterns remain Python **`matcher`**
constructors).

### Phase 7 — Special-form dispatch integration *(in progress)*

**Landed for the `Syntax` box:** **`compile/define-syntax`** (and related
**`compile/syntax-match`**, macro-rules, special-rules builders) compile to
**`syntax.Syntax`** with per-arm frozen matchers and the expansion pipeline
described under "Syntax NG" above.

**Still pending in the Phase 7 spirit:** migrating every other special that
still relies on one-off matching instead of shared **`matcher`** frozen
matchers (or a single shared funnel), and tightening const-pool story for all
compiled specials—not only syntax boxes.

Per-special-form authoring remains either Python **`matcher`** builders or,
after broader **`define-matcher`** integration, surface matcher syntax lowered
through Phase 6.

### Phase 8 — Runtime match/case *(implemented)*

Runtime `match` is a **sequence-unpacking** form over the same matcher
constants. Opcodes / lowering in [match-case-form.md](match-case-form.md); the
surface pattern language (clause delimiter, head-driven classifier,
reader-sugar heads, guards, and the universal shape-detection vision) in
[match-pattern-language.md](match-pattern-language.md). The special makes no
shape decisions — all pattern classification is the matcher's, dispatched on
head. `*: name` rest is a matcher-surface rule on the spine composites
(`form_factory` / `match_factory`), not a match-only convenience.

One opcode carries the lowering, styled on the `SWITCH_EQ` chain: **`MATCH_CASE`**
runs an arm's frozen matcher against the subject held on the stack; no-match keeps
the subject and jumps, the first match pops it (unless a guard keeps it), binds the
arm's captures into the namespace, and pushes nothing. The arm matcher rides as a
frozen 6-tuple const; `MATCH_CASE` thaws it once into a **`KOS_CACHE_MATCHER`**
per-site cache slot, which also holds a `KosSlotMap` of the capture slots resolved
against the namespace layout (the thaw is a pure function of the const and needs no
guard; the slot map carries its own layout guard). Arm binding is inlined per arm,
so the seq-values-kwargs foundation is not on the critical path.

Runtime flow:

1. Evaluate subject once; keep it on the stack across the arm chain.
2. Per arm, `MATCH_CASE` re-adapts the subject and runs the arm matcher (adapt-once
   via a dynnode is a deferred optimization).
3. Run case matchers in order.
4. On the first match, `MATCH_CASE` binds the captures into the namespace through
   the cached slot map.
5. Execute the selected body.

### Phase 9 — Hygiene extraction migration *(partial)*

**`MatchResult`** still carries **captures only**; hygiene is not on the
result object. **`syntax`** expansion already pairs **`Hygiene`** (per
transform arm) with **`collect_binding_symbols`**, **`expand_template_with_hygiene`**,
and **`finalize_macro_hygiene_unbox_one_pass`** for template and procedural
macro output. Remaining Phase 9 work is about **other** specials and helpers
that still introspect legacy matcher shapes—migrate them to the same explicit
**`captures` + `Hygiene`** discipline (or delete dead code) rather than
reintroducing hygiene into **`MatchResult`**.

Map existing behavior to the split model for:

* lambda formals over body
* let bindings over body
* letrec bindings over body
* with/except/using-style forms
* macro expansion hygiene substitution

### Migration execution plan — baseline-first, single cutover *(agreed)*

The branch intent is to remove legacy matcher/hygiene and rename the
NG stack to canonical names. To avoid getting stuck mid-migration, the
work is staged as small, testable baseline changes first, then one
large cutover.

#### Baseline A — `hygiene.py` as a standalone engine

Add `hygiene.py` without changing production call sites. It should
provide the same high-level operations as current hygiene handling
(collect introduced symbols, recurse through scope targets, apply
substitution), but operate over explicit hygiene data rather than legacy
matcher-spec introspection.

#### Baseline B — `HygienicMatchCompiler` emits hygiene objects

Teach **`HygienicMatchCompiler`** (not the baseline **`MatchCompiler`**)
to produce a **`Hygiene`** object as a side product of lowering the
same pattern tree. This output is validated and tested directly, but is
not yet required by existing special-form dispatch.

#### Baseline C — minimal special-form funnel interface

Add a minimal interface on `Special` (not broad `Compiled`) that routes
matching/hygiene through a stable API. Update **`compile/specials/syntax.py`**
to call this funnel instead of directly depending on matcher internals. At this
point, specials may still be backed by legacy matcher/hygiene behavior,
but all callers go through one indirection surface.

**Note:** The long-term shape may be **`syntax.Syntax`** implementing
the same funnel contract for pattern-heavy forms, with **`Special`**
left as a thin legacy adapter until Cutover E. Baseline C can be
satisfied by a shared protocol (match + optional hygiene) that both
types satisfy.

#### Baseline D — reduced syntax-rules matcher compiler *(largely landed)*

**Done in tree:** **`koskari/syntax.py`** — **`parse_syntax_rules_pattern`**,
**`build-syntax`** / **`build-macro`** / **`build-special`** runtime constructors,
**`Syntax` / `SyntaxTransform` / `MacroTransform` / `SpecialTransform`** with
**`match_select`** dispatch on **`Syntax`**, **`_promote_repeat_frame_bindings`** for compound ellipsis,
and **`_finalize_macro_output`** (binding-symbol discovery + one-pass Datum
peel / introduced renames). Tagged const-pool **`serialize_form`** /
**`deserialize_form`** rows and **`to_syntax_rules_tagged`** are **gone**;
recompilation lowers to quoted **`parse-rule-pattern`** / **`parse-match-pattern`**
plus **`compile/quote`** template segments. **`koskari/compile/specials/syntax.py`** emits
those forms for **`compile/define-syntax`** (and related match surfaces).
**`koskari/importer/bootstrap.py`** registers **`build-syntax`**, **`build-macro`**, and
**`build-special`** (hyphenated builtin names).

**Remaining / hardening:** broaden regression coverage for edge **`Datum`**
shapes and multi-arm **`Syntax`** ordering; audit any macro handlers that still
expect legacy **`RepeatResult`**-style kwargs instead of raw **`captures`**;
optional performance work (cached lowers for **`helpers.match`** static
patterns, etc.—see tune-up notes in reviews, not normative here). Cutover E
**landed:** legacy **`koskari.syntax`**, **`koskari.matcher`**, **`koskari.hygiene`**,
**`koskari.compile.helpers`**, and **`koskari/compile/helpers.py`** are removed from
the tree.

#### Cutover E — migrate all specials and switch backend

Perform the large migration in one step (historical checklist; steps 1–5 are
**done** in tree):

1. Re-declare special-form patterns using the new matcher API
2. Update `@special_forms` plumbing to attach the hygiene object
3. Switch special funnel methods to the new matcher/hygiene APIs
4. Update syntax/macro rule paths to **`syntax`**: **`Syntax`** dispatch,
   native matchers, and **`hygiene`** only
5. Remove legacy matcher, legacy syntax transformer glue, legacy hygiene, and
   legacy **`compile/helpers.py`**
6. Rename NG modules to canonical names (**open**; **`matcher`** /
   **`syntax`** names remain for now)

This cutover is intentionally large, but the preceding baselines ensure
all framework pieces are already present and individually tested.

---

## Remaining Design Decisions

1. **Sentinel for failed `OPTIONAL` captures.** *Resolved.* Slots
   that an `OPTIONAL` arm did not bind appear as `ABSENT` (the
   `koskari.lib.ABSENT` singleton) in `MatchResult.captures`.
2. **Duplicate capture-name behavior outside named repeats.**
   *Resolved.* Duplicate slot names raise `ValueError` at
   freeze time. Each `ONEOF` branch is its own slot scope, so
   identical names across mutually exclusive arms remain valid.
3. **Whether named composites are always inlined before freeze or
   whether a low-level `CALL_MATCHER` node exists.** *Resolved.*
   Composites are always inlined; the frozen tuple is pure
   primitives. There is no `CALL_MATCHER` node kind, and no
   intent to add one.
4. **Improper-list policy for KosPair syntax matching.** *Resolved.*
   `FORM` requires a proper list at the engine level; `match_form`
   in `koskari/lib/matcher.c` rejects improper-list subjects with
   a soft no-match. Improper-list `FORMALS` are therefore
   structurally rejected, which is the intended koskari semantics.
5. **Field extraction semantics for `CLASS`.** *Resolved.*
   Positional fields use `__match_args__`; keyword fields use
   `name=`. `AttributeError` during attribute extraction folds
   to a no-match.
6. **Whether shorthand `(Point x: 0 y)` is accepted or only
   canonical `(CLASS Point x: 0 y)`.** *Open.* The landed
   Phase 6 parser recognises only the canonical `(CLASS ...)` form;
   the meta-matcher sketch in this document recognises only the
   canonical `(CLASS ...)` form.
7. **Whether source diagnostics use best-failure tracing inside the
   matcher or an external retry/tracing mode.** *Open.* Deferred;
   no diagnostic surface beyond the boolean `matched` flag exists
   yet. A trace mode could be threaded into the engine without
   changing the public match contract.
8. **`GROUP` vs `MATCH`.** *Resolved.* The Python builder exports
   **`MATCH(*children)`** for spine sequencing and **`GROUP(slot, …)`**
   for nested dict frames (`slot` optional). Both thaw to native
   **`KOS_MATCH_NODE_MATCH`** and **`KOS_MATCH_NODE_GROUP`** respectively.
   The Phase 6 surface parser may treat **`MATCH`**-headed forms as
   grouping sugar where the grammar requires it.

---

## Working Summary

The matcher system should be a compiled, frozen, native rule program with ergonomic syntax and builder layers above it. The same rules can match runtime values and syntax forms, but through different subject adapters. **Matching is pure:** the frozen constant and **`MatchResult`** carry the rule program and **captures** only. **Hygiene** is optional metadata assembled in parallel (**`Hygiene`** / `hygiene`) when a pattern is lowered with **`HygienicMatchCompiler`**, not a by-product of the native match step itself. Named matchers are compile-time conveniences that lower into primitive rules. Runtime symbolic dependencies such as classes and types are provided explicitly as extern bindings at match invocation. The low-level matcher should remain small, array-based, acyclic, and strict about validation so matching is fast and failures are ordinary no-match results.

**Syntax and macros** for **`compile/define-syntax`** already live on
**`syntax`**: RULE patterns lower to **`matcher`**, and **`Syntax`** is
the multi-pattern **`Compiled`** dispatcher (**template + finalize**,
**macro handler + finalize**, **special**). New work should assume
**`syntax`** + **`hygiene`** for syntax-rule boxes; the legacy Python
**`koskari.syntax`** / **`koskari.matcher`** stack is deleted.
