# Object operator specials

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

The forms in this section all map directly to `OBJECT_*` opcodes. They are
implemented in `koskari/compile/specials/object.py` and tested in
`tests/compile/specials/test_object_specials.py`.



### Constant folding

Reducible (COUNT) operators apply left-to-right constant folding: consecutive
leading arguments that compile to a single `CONST` are folded together using
the corresponding Python operator before any code is emitted. Folding stops at
the first non-constant argument to preserve dunder dispatch semantics.

```scheme
(compile/+ 1 2 a)   ; → CONST 3, VAR a, OBJECT_ADD 2
(compile/+ 1 2)     ; → CONST 3, OBJECT_ADD 1
(compile/+ a 1 2)   ; → VAR a, CONST 1, CONST 2, OBJECT_ADD 3  (no fold)
```

`compile/**`, `compile/instance?`, and `compile/subclass?` never fold
(non-trivial multi-argument semantics).

---

---

Source: [design/SPECIALS.md](../../../design/SPECIALS.md)
