# Pattern Matching and Template Expansion API

This document describes a data-driven pattern matching and template expansion system for Koskari. It serves two purposes:

1. **Simplify current special-form handlers** by replacing manual pair unpacking, keyword checks, and structural validation with declarative pattern definitions.
2. **Foundation for define-syntax** by providing a representation that both Python and future Koskari code can consume for syntax macros.

All pattern and template structures use **tagged tuples**: every element is `(tag, ...payload)` where the tag identifies the element type. This design:

- Avoids ambiguity between literals and bindings.
- Serializes to const-pool-friendly koskari values (symbols, keywords, pairs).
- Is consumable from Python (objects with `to_tuple()` / `from_tuple()`) and from Koskari (direct tuple interpretation).

**Related:** [../language/unified-matcher-transformer-model.md](../language/unified-matcher-transformer-model.md) — **RULE** vs **MATCH** macro pattern languages, unified transformer pipeline, and Matcher IR for specials, `define-syntax`, and procedural macros.

**MATCH macro patterns vs this document’s `MATCH(...)`:** The **MATCH** pattern
language used in **`syntax-match`** / **`macro-match`** (per-rule **`PATTERN`**
forms, lowered by `parse_explicit_rule_pattern` in `koskari/matcher/heads.py`) is
**Koskari syntax for the same matcher IR** described here. Specials build
matchers with the Python **`MATCH(...)`** combinator below; macro authors build
**the same structures** from source. The names align **by design**—expressive
fidelity is the point.

---

## Implementation

The Tagged Tuple Representation, Tags, Match Result Types and Match Result
sections further down record the design this note proposed and the IR of
the Python matcher it was written against (`matcher/...` tags,
`to_tuple()` / `from_tuple()`, results carrying `.source`, `with_specs`);
the shipped matcher has none of it. Patterns lower to node tables, and
[MATCHER.md](../MATCHER.md) supersedes those sections.

A pattern is lowered to its node tables by **`koskari.matcher.tables`**, analysed by **`koskari.matcher.analysis`**, and emitted as `MATCH_*` instructions by **`koskari.matcher.emitter`** ([../archive/matcher-core.md](../archive/matcher-core.md)); **`koskari.matcher.build`** completes the instructions into a matcher `Function`, which returns the captures or `None`. Python-side pattern authoring uses **`koskari.matcher`** (builders such as `LIST`, `MATCH`, `FORM`, `SYMBOL`, `FORMALS`, …) and **`koskari.compile.helpers`** for compile helpers (`gather_bindings`, `gather_formals`, …).

`koskari.hygiene` holds template transcription, marking, and resolution; `koskari.syntax` the transformer types.

```python
from koskari.matcher import LIST, MATCH, SYMBOL, FORMALS, make_matcher
from koskari.compile.helpers import gather_formals
```

Compile-specific composites (e.g. **BINDINGS**, **FORMALS**) are defined in **`koskari.matcher.forms`** and imported through **`koskari.compile.helpers`** where compile code needs a single import surface.

**Current usage**: specials use `syntax` and `helpers`; see [`koskari/compile/specials/`](../../koskari/compile/specials/) and [`koskari/syntax.py`](../../koskari/syntax.py).

---

## Design Goals (Target Matcher Set)

The following describes the intended unified design. Implementation may lag; see "Implementation" for current state; the Tags tables are
the superseded IR.

### Unified Coverage

All matchers follow a regex MatchObject-like model: forms bind by key in the result dict, and some have special unpacking semantics (e.g. BINDINGS yields `(names, values)`).

**Core matchers (the frozen IR; built via `koskari.matcher`):**

| Matcher | Keyed? | Result | Notes |
|---------|--------|--------|-------|
| **BIND** | yes | `{slot: element}` | Any single element |
| **SYMBOL** | yes | `{slot: symbol}` | Renamed from BIND_SYM |
| **PAIR** | yes | `{slot: pair}` | Capture whole pair (BIND_PAIR renamed); type-constrained |
| **REST** | yes | `{slot: remainder}` | Remainder of list |
| **EXPR** | yes | `{slot: element}` | Single form; scope region for hygiene |
| **LITERAL** | no | — | Exact match, no capture |
| **OPT** | structural | — | Optional sub-pattern |
| **ANY_ORDER** | structural | — | Order-independent sequence |
| **MATCH** | structural / optional key | — | Sequence match; see below |

**Compile-specific matchers (`koskari.matcher` / `koskari.compile.helpers`):** BINDINGS, BINDING, FORMALS, DEFINE (and related composites). These participate in the same hygiene wire as other `matcher` patterns.

### MATCH as Structural Form for Pairs

MATCH always means "walk a sequence and match specs in order." The *source* varies by context:

- **Top-level**: The sequence is the form being matched.
- **Sub-expression**: The current element must be a **pair**. The pair `(car . cdr)` is treated as a 2-element sequence `[car, cdr]`, and MATCH's specs run against that.

There is no separate structural `PAIR(car_spec, cdr_spec)`. Instead, MATCH itself serves that role when used as a sub-expression. Keyword clauses become nested MATCH:

```python
MATCH(
    MATCH(LITERAL_KW("before:"), BIND("before_body")),
    MATCH(LITERAL_KW("body:"), BIND("body")),
    MATCH(LITERAL_KW("after:"), BIND("after_body")),
)
```

The inner MATCHs receive a pair; they destructure it and match car/cdr. Pair destructuring applies only when MATCH has 2+ specs; with 1 spec the element is passed through (e.g. PAIR captures the pair).

### MATCH Optional Key

When MATCH is a sub-expression, an optional `key` captures the matched pair:

```python
MATCH(LITERAL_KW("before:"), BIND("before_body"), key="before_clause")
```

Then `result["before_clause"]` is the whole `(before: . ...)` pair and `result["before_body"]` is the cdr. Python requires keyword args last, so the key appears after the specs.

### PAIR vs MATCH

- **PAIR(slot)** — capture the whole pair into a slot. No destructuring.
- **MATCH(...)** as sub-expr — element must be pair; destructure and match (car, cdr) against inner specs.

---

## Tagged Tuple Representation

### Canonical Form

Every pattern or template element is a tagged tuple:

```
(tag, ...payload)
```

- **Tag**: a symbol in the `matcher/` namespace (e.g. `symbol("matcher/match")`, `symbol("matcher/literal")`).
- **Payload**: koskari values (symbols, keywords, pairs, nested tagged tuples).

### Round-Trip

- **Python API**: Builder objects (e.g. `MATCH(...)`, `BIND("x")`) with `to_tuple()` → tagged tuple, and `from_tuple(t)` → restore object via `MATCHERS` registry.
- **Koskari**: Patterns stored as constant data; the matcher interprets tagged tuples via `from_tuple()`.

---

## Pattern Matching

### Matcher Protocol

Implementations provide:

```python
def match(self, source, literals, result, specs=None) -> Tuple[Optional[Any], bool]:
    """On success: mutate result with bindings, return (remainder, True). On failure: (None, False).
    When specs is not None, capturing matchers add specs[key] = self."""
```

### Tags (frozen matcher IR)

| Tag | Payload | Meaning |
|-----|---------|---------|
| `matcher/match` | elem, elem, ... [, key] | Sequence match; when sub-expr, element must be pair, destructure (car, cdr). Optional key captures matched pair. |
| `matcher/literal` | value | Match this value exactly (keyword or symbol) |
| `matcher/bind` | slot | Capture one element; bind MatchResult(source, source_pos) to `slot` |
| `matcher/bind-sym` | slot | Capture symbol (SYMBOL) |
| `matcher/bind-pair` | slot | Capture pair; same as `matcher/pair` |
| `matcher/rest` | slot | Capture remainder of list into `slot` |
| `matcher/expr` | slot | Capture single form; scope region |
| `matcher/pair` | slot | Capture whole pair; no structural form |
| `matcher/opt` | elem | Optionally match elem (succeed if absent) |
| `matcher/any-order` | elem, elem, ... | All elements must match, in any order |

### Tags (compile composites in matcher / helpers)

| Tag | Payload | Meaning |
|-----|---------|---------|
| `matcher/bind-bindings` | slot | Element must be a list; apply `gather_bindings`; bind BindingsResult (names, values, source, source_pos). |
| `matcher/bind-binding` | slot | Single binding; `(name, value)` |
| `matcher/formals` | slot | Formal parameters for lambda; bind FormalsResult (positional, defaults, kwonly, star, starstar, source, source_pos). |
| `matcher/define` | slot | Single define-style name; gensym'd (planned) |

### Helpers

- **LITERAL_KW(str)**: Returns `LITERAL(keyword(str))`.
- **LITERAL_SYM(str)**: Returns `LITERAL(symbol(str))`.

Keywords in the source are matched literally; only symbols are ambiguous (literal vs. binding). The matcher accepts a `literals` set of symbols to treat as literal; all others are binding slot names.

### Clause Forms

- **Keyword clause** (target): `MATCH(LITERAL_KW("before:"), BIND("before_body"))` — when MATCH is a sub-expr, the element must be a pair; destructure and match. (Current: `PAIR(car_spec, cdr_spec)`.)
- **Positional**: `BIND("body")` — capture one element.
- **Rest**: `REST("tail")` — capture remainder of list.
- **Type-constrained**: `SYMBOL("name")` for symbols, `PAIR("bindings")` for whole pairs, `BINDINGS("bindings")` for binding lists (pre-parsed).

### Match Result Types

Every capture yields a structured result object with `source` (raw matched value) and `source_pos` (position when available):

* **MatchResult** — base class for simple captures (BIND, SYMBOL, DEFINE, VALUE, PAIR, REST, EXPR). Attributes: `source`, `source_pos`. Use `result["key"].source` for the raw value.
* **FormalsResult** — for FORMALS. Attributes: `source`, `source_pos`, `positional`, `defaults`, `kwonly`, `star`, `starstar`. Supports unpacking: `pos, defaults, kwonly, star, starstar = matched["formals"]`.
* **BindingsResult** — for BINDINGS. Attributes: `source`, `source_pos`, `names`, `values`. Supports unpacking: `names, values = matched["bindings"]`.

All capturing matchers store these result types. Use `.source` when the raw form is needed (e.g. for lambda construction or position reporting).

### Example: let form (implemented)

The let special uses `SYMBOL`, `BINDINGS`, and `REST`:

```python
_LET_PATTERNS = [
    MATCH(SYMBOL("name"), BINDINGS("bindings"), REST("body")),
    MATCH(BINDINGS("bindings"), REST("body")),
]
matched = match(rest, _LET_PATTERNS)
# matched["bindings"] is BindingsResult; unpack: args, vals = matched["bindings"]
# matched["body"].source is the raw body list
```

### Example: dyn-wind (proposed, target syntax)

```python
# Python API — target design: MATCH as sub-expr destructures pairs
PATTERNS = [
    MATCH(
        ANY_ORDER(
            MATCH(LITERAL_KW("before:"), BIND("before_body")),
            MATCH(LITERAL_KW("body:"), BIND("body")),
            MATCH(LITERAL_KW("after:"), BIND("after_body")),
        ),
    ),
    MATCH(
        ANY_ORDER(
            MATCH(LITERAL_KW("body:"), BIND("body")),
            MATCH(LITERAL_KW("after:"), BIND("after_body")),
        ),
    ),
    # ... other permutations
]
```

Current implementation uses `PAIR(car_spec, cdr_spec)`; the target collapses that into MATCH-as-sub-expr.

Serialized (tagged tuple, target):

```python
(symbol("matcher/match"),
 (symbol("matcher/any-order"),
  (symbol("matcher/match"), (symbol("matcher/literal"), keyword("before:")), (symbol("matcher/bind"), symbol("before_body"))),
  (symbol("matcher/match"), (symbol("matcher/literal"), keyword("body:")), (symbol("matcher/bind"), symbol("body"))),
  (symbol("matcher/match"), (symbol("matcher/literal"), keyword("after:")), (symbol("matcher/bind"), symbol("after_body")))),
)
```

### Match Result

On success, the matcher returns a dict (or equivalent mapping):

```python
{"before_body": pair_or_nil, "body": pair_or_nil, "after_body": pair_or_nil}
```

Missing optional slots may be `None` or absent. The handler checks presence and compiles accordingly.

When `match(..., with_specs=True)`, the matcher returns `(result_dict, specs_dict)` where `specs_dict[key]` is the matcher instance that produced `result_dict[key]`. Used by hygiene discovery to identify binder vs scope-region roles.

---

## Template Expansion

Templates describe how to produce output (source forms) from bindings.

### Tags

| Tag | Payload | Meaning |
|-----|---------|---------|
| `ref` | slot | Splice binding value into output |
| `literal` | form | Emit this form as-is |
| `gen` | name | Emit fresh gensym for this binding name (hygiene) |
| `cons` | car_spec, cdr_spec | Build pair from two template specs |

### Example

```python
# Expand (my-macro x y) -> (list x y)
TEMPLATE = CONS(REF("x"), CONS(REF("y"), LITERAL(nil)))
```

Serialized:

```python
(symbol("cons"),
 (symbol("ref"), symbol("x")),
 (symbol("cons"),
  (symbol("ref"), symbol("y")),
  (symbol("literal"), nil)))
```

---

## API for Python Consumption

### Builder Objects

Construct patterns ergonomically and build each pattern's matcher once:

```python
from koskari.lib import cons, demand, nil
from koskari.matcher import LIST, LITERAL_KW, MATCH, SYMBOL, make_matcher

matcher = demand(make_matcher(MATCH(LIST(SYMBOL("name"), LITERAL_KW("as")))))
captures = matcher(cons(form, nil))   # the captures, or None
```

`first_match(subject, matchers)` tries several matcher functions in
order and returns the first hit's index and captures, or `(-1, None)`.
A matcher function is reentrant and holds no state, so a module builds
each pattern's matcher once at import, as `koskari.matcher.forms` does
for `BINDING_MATCHERS`.

The tagged-tuple `match`, `from_tuple` and `match_tuple` functions this note
proposed were never the shipped API.

### Expand Function (Future)

```python
def expand(template, bindings, gensyms=None):
    """
    Walk template (tagged tuple), substitute REF/GEN, emit result.
    Not yet implemented.

    :param template: tagged tuple or builder object
    :param bindings: dict from match
    :param gensyms: dict for hygiene (name -> gensym); mutated in place
    :return: expanded form (pair/tree)
    """
```

---

## API for Koskari Consumption (define-syntax)

Future `define-syntax` will compile source definitions into tagged tuples stored in the module. The syntax compiler:

1. Parses `define-syntax` forms.
2. Compiles pattern and template source into tagged tuples.
3. Registers them for expansion at compile time.

The **same** match and expand logic interprets these tuples. No separate implementation.

### Stored Representation

A syntax rule might be stored as:

```python
(
    symbol("syntax-rule"),
    (symbol("match"), ...),   # pattern
    (symbol("cons"), ...),    # template
)
```

Or as a list of (pattern, template) pairs for multiple clauses.

### Koskari-Side Builders (Optional)

Koskari could provide forms that construct tagged tuples:

```lisp
;; Target: MATCH destructures pairs when sub-expr
(syntax/pattern
  (syntax/match
    (syntax/match (syntax/literal-kw "before:") (syntax/bind 'before-body))
    (syntax/opt (syntax/match (syntax/literal-kw "after:") (syntax/bind 'after-body)))
    (syntax/rest 'body)))
```

These expand to the same tagged tuples the matcher expects.

---

## Integration with Compiler Pipeline

### Current Specials

1. Compiler dispatches to `compile_special(fragment, comp, source_obj)`, which calls `comp.compile_impl(compiler, source_obj)`.
2. Special handler receives `source_obj` (the full form, e.g. `(compile/let ...)`).
3. **With patterns**: Handler calls `match(tail, PATTERNS, literals)`; on success, uses bindings to compile.

**Current adopters**: specials under `compile/specials/` declare `patterns=` on `@special`, matched through the `Syntax`'s `first_match`, and use `helpers` (`gather_bindings`, `gather_formals`, …) with matchers built once at import.

### Future Macros

1. Compiler sees `(my-macro ...)`.
2. Looks up `my-macro` in syntax table → finds (pattern, template) tuples.
3. Calls `match(source, pattern)`.
4. On match, calls `expand(template, bindings)`.
5. Feeds expanded form back into compiler (same path as specials that return expressions).

### Shared Machinery

- Pattern matcher: used by both specials and macros.
- Template expander: used only by macros (specials compile directly to bytecode).
- Tagged tuple format: shared by both.

---

## Implementation Notes

### MATCHERS Registry

Pattern heads register in `MATCHER_REGISTRY` (`koskari.matcher.heads`) and lower through **`TableBuilder`** to node tables, which **`koskari.matcher.emitter`** alone consumes. Legacy Python `@matcher` registration is gone.

### Const-Pool Friendliness

- Tags are symbols in the `matcher/` namespace.
- Payloads are keywords, symbols, pairs, nil.
- Patterns can live in Python module scope (created at import) or in koskari constant pools.

### Error Reporting

- Match failure should surface which pattern was tried and where it failed (position in source).
- Consider optional `expected` strings per pattern for user-facing errors.

### Performance

- First-match semantics: order patterns by specificity if needed.
- Python builders: create tuples once; matcher interprets only.
- For hot paths, consider caching compiled pattern representations (e.g. flattened state machine).

---

## Related Documents

- [../language/unified-matcher-transformer-model.md](../language/unified-matcher-transformer-model.md): Proposed shared Matcher IR and transformer pipeline (syntax-rules, procedural macros, specials)
- [../archive/matcher-hygiene-extensions.md](../archive/matcher-hygiene-extensions.md): MATCH role additions for hygiene discovery; `BINDINGS` with scope; REST/EXPR as body regions (archived; superseded by hygiene by marks)
- [archive/macros.md](../archive/macros.md): Macro system notes; define-syntax will supersede defmacro
- [special-forms.md](../language/special-forms.md): Special form catalog
- [types.md](types.md): symbol, keyword, pair — types used in patterns
