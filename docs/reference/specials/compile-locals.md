# compile/locals
**Symbol:** `compile/locals`

**Syntax:** `(locals)`

**Use-case:** Obtain the current frame's namespace (locals), analogous to
Python's `locals()`.

**Behaviour:** Emits `LOCALS` (argument 0). Pushes the current frame's namespace
onto the stack. Any arguments are rejected with `SpecialError`.

**Examples:**

```koskari
(locals)
```

**Implementation:** `koskari/compile/specials/variables.special_locals`


