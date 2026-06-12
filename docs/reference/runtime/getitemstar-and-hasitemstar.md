# getitem* and hasitem*

Runtime builtins for chained item access. These complement the
compile-time **`item*`** macro: **`keylist`** is a runtime sequence, and
failed lookups do not expand to nested **`item`** forms.

## getitem*

**Syntax:** `(getitem* ELEMENT KEYLIST)` or `(getitem* ELEMENT KEYLIST DEFAULT)`

Walk **`ELEMENT`** through each key in **`KEYLIST`** left to right (same
nesting shape as **`item*`**).

When **`DEFAULT`** is supplied, return it on **`LookupError`** at any step
(including **`KeyError`** and **`IndexError`**). When **`DEFAULT`** is
omitted, **`LookupError`** propagates. Other exceptions always propagate.

An empty **`KEYLIST`** raises **`ValueError`**.

```koskari
(getitem* tbl #["a" "b"])
(getitem* tbl #["a" "b"] missing)
(getitem* vec #("0" "1"))
```

## hasitem*

**Syntax:** `(hasitem* ELEMENT KEYLIST)`

Walk **`ELEMENT`** through each key in **`KEYLIST`** left to right. Return
true only when every step succeeds; return false on the first failed lookup
without raising.

Any exception during lookup is treated as absent.

An empty **`KEYLIST`** raises **`ValueError`**.

```koskari
(hasitem* tbl #["a" "b"])
(hasitem* vec #("0" "1"))
```

## Comparison

| Form | Kind | On missing link |
|------|------|-----------------|
| **`item*`** | compile-time macro | raises (via **`item`**) |
| **`getitem*`** | runtime builtin | **`DEFAULT`** on **`LookupError`**, or raises without default |
| **`hasitem*`** | runtime builtin | false |
| **`getattr*`** | runtime builtin | chained attribute access (see [getattr* and hasattr*](getattrstar-and-hasattrstar.md)) |

See also [compile/item* and compile/attr*](../macros/compile-itemstar-and-compile-attrstar.md).
