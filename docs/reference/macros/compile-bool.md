# compile/bool

**Syntax:** `(bool VALUE)`

**Behaviour:** Expands to `(truth VALUE)`: the truthiness of `VALUE`
under Python's rules, as `True` or `False`.

**Examples:**

```koskari
koskari> (bool 0)
False
koskari> (bool "x")
True
koskari> (bool #[])
False
```

**See also:** [`?`](compile-q.md), which tests and branches in one form.
