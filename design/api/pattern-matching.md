# Pattern Matching and Template Expansion API

This document describes a data-driven pattern matching and template expansion system for Koskari. It serves two purposes:

1. **Simplify current special-form handlers** by replacing manual pair unpacking, keyword checks, and structural validation with declarative pattern definitions.
2. **Foundation for define-syntax** by providing a representation that both Python and future Koskari code can consume for syntax macros.

All pattern and template structures use **tagged tuples**: every element is `(tag, ...payload)` where the tag identifies the element type. This design:

- Avoids ambiguity between literals and bindings.
- Serializes to const-pool-friendly koskari values (symbols, keywords, pairs).
- Is consumable from Python (objects with `to_tuple()` / `from_tuple()`) and from Koskari (direct tuple interpretation).

---

## Implementation (Current)

The matcher lives in **koskari.matcher**. Builder functions and the `match()` entry point are imported from there:

```python
from koskari.matcher import MATCH, BIND, BIND_SYM, BIND_PAIR, REST, PAIR, OPT, ANY_ORDER
from koskari.matcher import LITERAL, LITERAL_KW, LITERAL_SYM
from koskari.matcher import match, from_tuple, match_tuple
```

A compile-specific matcher, **BIND_BINDINGS**, lives in **koskari.compile.helpers** (it depends on `gather_bindings`):

```python
from koskari.compile.helpers import BIND_BINDINGS
```

Matcher classes are registered via the `@matcher(tag)` decorator into a `MATCHERS` dict. Tagged tuples are deserialized via `from_tuple(tup)`, which looks up the tag in `MATCHERS` and calls the class `from_payload()`.

**Current usage**: The let form and `helper_binding` use the matcher. See `koskari/compile/specials/let.py` and `koskari/compile/helpers.py`.

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
def match(self, source, literals, result) -> Tuple[Optional[Any], bool]:
    """On success: mutate result with bindings, return (remainder, True). On failure: (None, False)."""
```

### Tags (koskari.matcher)

| Tag | Payload | Meaning |
|-----|---------|---------|
| `matcher/match` | elem, elem, ... | Top-level sequence; elements must appear in order |
| `matcher/literal` | value | Match this value exactly (keyword or symbol) |
| `matcher/bind` | slot | Capture one element into binding `slot` |
| `matcher/bind-sym` | slot | Capture one element into `slot` only when it is a symbol |
| `matcher/bind-pair` | slot | Capture one element into `slot` only when it is a pair |
| `matcher/rest` | slot | Capture remainder of list into `slot` |
| `matcher/pair` | car_spec, cdr_spec | Element must be pair; car matches car_spec, cdr bound per cdr_spec |
| `matcher/opt` | elem | Optionally match elem (succeed if absent) |
| `matcher/any-order` | elem, elem, ... | All elements must match, in any order |

### Tags (koskari.compile.helpers)

| Tag | Payload | Meaning |
|-----|---------|---------|
| `matcher/bind-bindings` | slot | Element must be a list; apply `gather_bindings` and bind `(names, values)` tuple to `slot` |

### Helpers

- **LITERAL_KW(str)**: Returns `LITERAL(keyword(str))`.
- **LITERAL_SYM(str)**: Returns `LITERAL(symbol(str))`.

Keywords in the source are matched literally; only symbols are ambiguous (literal vs. binding). The matcher accepts a `literals` set of symbols to treat as literal; all others are binding slot names.

### Clause Forms

- **Keyword clause**: `PAIR(LITERAL_KW("before:"), BIND("before_body"))` — match a pair whose car is the keyword, bind cdr to `before_body`.
- **Positional**: `BIND("body")` — capture one element.
- **Rest**: `REST("tail")` — capture remainder of list.
- **Type-constrained**: `BIND_SYM("name")` for symbols, `BIND_PAIR("bindings")` for pairs, `BIND_BINDINGS("bindings")` for binding lists (pre-parsed).

### Example: let form (implemented)

The let special uses `BIND_SYM` and `BIND_BINDINGS`:

```python
_LET_PATTERNS = [
    MATCH(BIND_SYM("name"), BIND_BINDINGS("bindings"), REST("body")),   # named let
    MATCH(BIND_BINDINGS("bindings"), REST("body")),                      # unnamed let
]
matched = match(rest, _LET_PATTERNS)
# matched["bindings"] is (args, vals) from gather_bindings; no manual call needed
```

### Example: dyn-wind (proposed)

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
(symbol("matcher/match"),
 (symbol("matcher/any-order"),
  (symbol("matcher/pair"), (symbol("matcher/literal"), keyword("before:")), (symbol("matcher/bind"), symbol("before_body"))),
  (symbol("matcher/pair"), (symbol("matcher/literal"), keyword("body:")), (symbol("matcher/bind"), symbol("body"))),
  (symbol("matcher/pair"), (symbol("matcher/literal"), keyword("after:")), (symbol("matcher/bind"), symbol("after_body")))),
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

Construct patterns ergonomically:

```python
from koskari.matcher import MATCH, PAIR, BIND, BIND_SYM, BIND_PAIR, REST, OPT, ANY_ORDER
from koskari.matcher import LITERAL_KW, LITERAL_SYM, LITERAL
from koskari.matcher import match, from_tuple, match_tuple
from koskari.compile.helpers import BIND_BINDINGS

# Build
pattern = MATCH(
    PAIR(LITERAL_KW("before:"), BIND("before_body")),
    OPT(PAIR(LITERAL_KW("after:"), BIND("after_body"))),
    REST("body"),
)

# Serialize
tup = pattern.to_tuple()

# Deserialize (for tagged tuples from koskari or stored forms)
matcher_obj = from_tuple(tup)

# Match (accepts builder objects, tagged tuples, or lists of patterns)
bindings = match(source, pattern, literals=set())
if bindings is None:
    raise SpecialError("expected before: and/or after: clauses", source)

# Convenience: deserialize and match in one step
bindings = match_tuple(source, tup, literals=set())
```

### Match Function

```python
def match(source, patterns, literals=None):
    """
    Try each pattern against source (pair or unpacked list).
    Returns dict of bindings on first match, else None.

    :param source: pair (proper list) or iterable of elements
    :param patterns: single matcher, tagged tuple, or sequence of patterns
    :param literals: set/frozenset of symbols to match literally; default empty
    :return: dict mapping slot names (str) to values, or None
    """
```

### from_tuple and match_tuple

```python
def from_tuple(tup):
    """
    Deserialize tagged tuple into matcher object. Dispatches via MATCHERS registry.

    :param tup: tagged tuple (tag, ...payload)
    :return: matcher object with match() and to_tuple()
    :raises ValueError: when tup is not a valid matcher tuple
    """

def match_tuple(source, tup, literals=None):
    """
    Deserialize tagged tuple to matcher, then match. Convenience for from_tuple + match.

    :param source: pair or iterable
    :param tup: tagged tuple pattern
    :param literals: set/frozenset; default empty
    :return: dict of bindings, or None
    """
```

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
2. Special handler receives `source_obj` (the full form, e.g. `(compile/let ...)`).
3. **With patterns**: Handler calls `match(tail, PATTERNS, literals)`; on success, uses bindings to compile.

**Current adopters**: The let form (`compile/specials/let.py`) and `helper_binding` (`compile/helpers.py`) use the matcher. Others (e.g. dyn-wind) still use manual unpacking.

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

Matcher classes register via `@matcher(tag)`. The `from_tuple()` function looks up the tag in `MATCHERS` and calls `cls.from_payload(payload, from_tuple)`. New matcher types can be added by defining a class with `match()`, `from_payload()`, and `to_tuple()`, then registering with the decorator. `BIND_BINDINGS` lives in `compile/helpers` but registers into `MATCHERS` so tagged-tuple deserialization works.

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

- [matcher-hygiene-extensions.md](matcher-hygiene-extensions.md): Proposed MATCH role additions (`BODY`, `BIND_SYM`, `BIND_BINDINGS`, `BIND_FORMALS`) for generic hygiene discovery driven by special-declared matcher metadata
- [archive/macros.md](../archive/macros.md): Macro system notes; define-syntax will supersede defmacro
- [special-forms.md](../language/special-forms.md): Special form catalog
- [types.md](types.md): symbol, keyword, pair — types used in patterns
