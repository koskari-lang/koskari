# compile/None? and compile/nil?
**Syntax:** `(None? VALUE)`, `(nil? VALUE)`

**Behaviour:** Identity predicates for the **`None`** and **`nil`** singletons. Expand to
**`(compile/is VALUE None)`** and **`(compile/is VALUE nil)`**.

`None?` tests **identity against `None`**, not truthiness. A value that is falsy
but is not `None` -- `0`, `""`, an empty collection -- is **not** `None`, so
`None?` returns false for it:

```koskari
(None? None)   ; => True
(None? 0)      ; => False
(None? "")     ; => False
(None? #[])    ; => False
```

Do not reach for `None?` when you mean "is this falsy"; those are different
questions. Use `None?` only to detect the `None` singleton, and test truthiness
directly (a bare `VALUE` in a conditional) when that is what you want.
