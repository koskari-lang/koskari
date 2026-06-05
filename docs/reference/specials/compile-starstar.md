# compile/**
**Symbol:** `compile/**`

**Syntax:** `( ** BASE EXP [MOD])`

**Use-case:** Exponentiation via `__pow__`. Optional third argument is the
modulus passed to three-argument `pow`.

**Behaviour:** Does not constant-fold. Emits `OBJECT_POWER`.

**Examples:**

```koskari
(** 2 10)        ; 1024
(** 2 8 1000)    ; modular exponent when MOD is given
```

See [Object operator overview](object-operators/index.md).
