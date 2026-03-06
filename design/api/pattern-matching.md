# Pattern Matching and Template Expansion API

This document describes a data-driven pattern matching and template expansion system for Koskari. It serves two purposes:

1. **Simplify current special-form handlers** by replacing manual pair unpacking, keyword checks, and structural validation with declarative pattern definitions.
2. **Foundation for define-syntax** by providing a representation that both Python and future Koskari code can consume for syntax macros.

All pattern and template structures use **tagged tuples**: every element is `(tag, ...payload)` where the tag identifies the element type. This design:

- Avoids ambiguity between literals and bindings.
- Serializes to const-pool-friendly koskari values (symbols, keywords, pairs).
- Is consumable from Python (objects with `to_tuple()` / `from_tuple()`) and from Koskari (direct tuple interpretation).

---

## Tagged Tuple Representation

### Canonical Form

Every pattern or template element is a tagged tuple:

```
(tag, ...payload)
```

- **Tag**: a symbol (e.g. `symbol("match")`, `symbol("literal")`, `symbol("bind")`).
- **Payload**: koskari values (symbols, keywords, pairs, nested tagged tuples).

Tags use a reserved symbol namespace (e.g. `symbol("pattern/match")`) or a prefix to avoid collision with user binding names.

### Round-Trip

- **Python API**: Builder objects (e.g. `MATCH(...)`, `BIND("x")`) with `to_tuple()` → tagged tuple, and `from_tuple(t)` → restore object (or interpret directly).
- **Koskari**: Patterns stored as constant data; the matcher/expander interprets tagged tuples directly.

---

## Pattern Matching

### Tags

| Tag | Payload | Meaning |
|-----|---------|---------|
| `match` | elem, elem, ... | Top-level sequence; elements must appear in order |
| `literal` | value | Match this value exactly (keyword or symbol) |
| `bind` | slot | Capture one element into binding `slot` |
| `rest` | slot | Capture remainder of list into `slot` |
| `pair` | car_spec, cdr_spec | Element must be pair; car matches car_spec, cdr bound per cdr_spec |
| `opt` | elem | Optionally match elem (succeed if absent) |
| `any-order` | elem, elem, ... | All elements must match, in any order |

### Helpers

- **LITERAL_KW(str)**: Serializes to `(literal, keyword(str))`.
- **LITERAL_SYM(str)**: Serializes to `(literal, symbol(str))`.

Keywords in the source are matched literally; only symbols are ambiguous (literal vs. binding). The matcher accepts a `literals` set of symbols to treat as literal; all others are binding slot names.

### Clause Forms

- **Keyword clause**: `(pair, (literal, keyword("before:")), (bind, symbol("before_body")))` — match a pair whose car is the keyword, bind cdr to `before_body`.
- **Positional**: `(bind, symbol("body"))` — capture one element.
- **Rest**: `(rest, symbol("tail"))` — capture remainder of list.

### Example: dyn-wind

```python
# Python API
PATTERNS = [
    MATCH(
        ANY_ORDER(
            PAIR(LITERAL_KW("before:"), BIND("before_body")),
            PAIR(LITERAL_KW("body:"), BIND("body")),
            PAIR(LITERAL_KW("after:"), BIND("after_body")),
        ),
    ),
    MATCH(
        ANY_ORDER(
            PAIR(LITERAL_KW("body:"), BIND("body")),
            PAIR(LITERAL_KW("after:"), BIND("after_body")),
        ),
    ),
    # ... other permutations
]
```

Serialized (tagged tuple):

```python
(symbol("match"),
 (symbol("any-order"),
  (symbol("pair"), (symbol("literal"), keyword("before:")), (symbol("bind"), symbol("before_body"))),
  (symbol("pair"), (symbol("literal"), keyword("body:")), (symbol("bind"), symbol("body"))),
  (symbol("pair"), (symbol("literal"), keyword("after:")), (symbol("bind"), symbol("after_body")))),
)
```

### Match Result

On success, the matcher returns a dict (or equivalent mapping):

```python
{"before_body": pair_or_nil, "body": pair_or_nil, "after_body": pair_or_nil}
```

Missing optional slots may be `None` or absent. The handler checks presence and compiles accordingly.

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

Construct patterns and templates ergonomically:

```python
from koskari.compile.patterns import MATCH, PAIR, BIND, REST, OPT, ANY_ORDER
from koskari.compile.patterns import LITERAL_KW, LITERAL_SYM
from koskari.compile.patterns import REF, CONS, LITERAL, GEN

# Build
pattern = MATCH(
    PAIR(LITERAL_KW("before:"), BIND("before_body")),
    OPT(PAIR(LITERAL_KW("after:"), BIND("after_body"))),
    REST("body"),
)

# Serialize
tup = pattern.to_tuple()

# Match (from tuple or object)
bindings = match(source, tup, literals=set())
if bindings is None:
    raise SpecialError("expected before: and/or after: clauses", source)
```

### Match Function

```python
def match(source, patterns, literals=None):
    """
    Try each pattern against source (pair or unpacked list).
    Returns dict of bindings on first match, else None.

    :param source: pair (proper list) or iterable of elements
    :param patterns: single tagged tuple or list of patterns
    :param literals: set of symbols to match literally (not as binding slots)
    :return: dict mapping slot names (str) to values, or None
    """
```

### Expand Function

```python
def expand(template, bindings, gensyms=None):
    """
    Walk template (tagged tuple), substitute REF/GEN, emit result.

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
(syntax/pattern
  (syntax/match
    (syntax/pair (syntax/literal-kw "before:") (syntax/bind 'before-body))
    (syntax/opt (syntax/pair (syntax/literal-kw "after:") (syntax/bind 'after-body)))
    (syntax/rest 'body)))
```

These expand to the same tagged tuples the matcher expects.

---

## Integration with Compiler Pipeline

### Current Specials

1. Compiler dispatches to `compile_special(fragment, comp, source_obj, tc)`.
2. Special handler receives `source_obj` (the full form, e.g. `(compile/dyn-wind ...)`).
3. **Today**: Handler manually unpacks, checks keywords, validates structure.
4. **With patterns**: Handler calls `match(tail, PATTERNS, literals)`; on success, uses bindings to compile.

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

### Const-Pool Friendliness

- Tags are symbols.
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

- [archive/macros.md](../archive/macros.md): Macro system notes; define-syntax will supersede defmacro
- [special-forms.md](../language/special-forms.md): Special form catalog
- [types.md](types.md): symbol, keyword, pair — types used in patterns
