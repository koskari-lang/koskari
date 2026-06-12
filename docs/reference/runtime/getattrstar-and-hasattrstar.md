# getattr* and hasattr*

Runtime builtins for chained attribute access. These complement the
compile-time **`attr*`** macro: **`attrlist`** is a runtime sequence, and
failed lookups do not expand to nested **`attr`** forms.

## getattr*

**Syntax:** `(getattr* ELEMENT ATTRLIST)` or `(getattr* ELEMENT ATTRLIST DEFAULT)`

Walk **`ELEMENT`** through each name in **`ATTRLIST`** left to right (same
nesting shape as **`attr*`**).

When **`DEFAULT`** is supplied, return it on **`AttributeError`** at any step.
When **`DEFAULT`** is omitted, **`AttributeError`** propagates. Other
exceptions always propagate.

An empty **`ATTRLIST`** raises **`ValueError`**.

```koskari
(getattr* obj #["a" "b"])
(getattr* obj #["a" "b"] missing)
(getattr* obj '("inner" "y"))
```

## hasattr*

**Syntax:** `(hasattr* ELEMENT ATTRLIST)`

Walk **`ELEMENT`** through each name in **`ATTRLIST`** left to right. Return
true only when every step succeeds; return false on the first failed lookup
without raising.

Per-step semantics match Python **`hasattr`**: any exception during lookup is
treated as absent.

An empty **`ATTRLIST`** raises **`ValueError`**.

```koskari
(hasattr* obj #["a" "b"])
(hasattr* obj '("inner" "y"))
```

## Comparison

| Form | Kind | On missing link |
|------|------|-----------------|
| **`attr*`** | compile-time macro | raises (via **`attr`**) |
| **`getattr*`** | runtime builtin | **`DEFAULT`** on **`AttributeError`**, or raises without default |
| **`hasattr*`** | runtime builtin | false |
| **`hasattr?`** | Python **`hasattr`** binding | single name only |

See also [compile/item* and compile/attr*](../macros/compile-itemstar-and-compile-attrstar.md).
