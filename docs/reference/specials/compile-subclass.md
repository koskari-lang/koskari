# compile/subclass?
**Symbol:** `compile/subclass?`

**Syntax:** `(subclass? CLS BASE ...)`

**Use-case:** Whether `CLS` is a subclass of `BASE` (or any base when several
are given). Requires at least class and one base type.

**Examples:**

```koskari
(subclass? list object)
(subclass? bool int object)   ; false for bool vs int
```

See [Object operator overview](object-operators/index.md).
