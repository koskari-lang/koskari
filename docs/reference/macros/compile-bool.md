# compile/bool

**Syntax:** `(bool VALUE)`

**Behaviour:** The truthiness of `VALUE` under Python's rules, as `True`
or `False`.

**Examples:**

```koskari
>>> (bool 0)
False
>>> (bool "x")
True
>>> (bool #[])
False
```

**See also:** [`?`](compile-q.md), which tests and branches in one form.

**Also:** type [`bool`](../runtime/types-and-predicates.md#numbers).
