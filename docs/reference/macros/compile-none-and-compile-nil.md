# compile/None?

**Syntax:** `(None? VALUE)`

**Behaviour:** Identity test against the `None` singleton: true when
`VALUE` is `None`.

`None?` tests identity, not truthiness. A value that is falsy but is not
`None` -- `0`, `""`, an empty collection -- is not `None`:

```koskari
>>> (None? None)
True
>>> (None? 0)
False
>>> (None? "")
False
>>> (None? #[])
False
```

Use `None?` to detect the `None` singleton, and test truthiness directly
(a bare VALUE in a conditional, or [`bool`](compile-bool.md)) when that is the
question.

**`nil?`** is the matching test for the `nil` singleton, and is a
[special](../specials/object-operators/index.md#unary-and-inspection-operators),
not a macro:

```koskari
>>> (nil? nil)
True
>>> (nil? None)
False
```

**Also:** function [`None?`](../runtime/functions.md#predicates).
