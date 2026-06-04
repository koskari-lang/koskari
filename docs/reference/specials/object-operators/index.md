# Object operator specials

The forms in this section map directly to `OBJECT_*` opcodes. They are
implemented in `koskari/compile/specials/object.py`.

## Constant folding

Reducible (COUNT) operators apply left-to-right constant folding: consecutive
leading arguments that compile to a single `CONST` are folded together using
the corresponding Python operator before any code is emitted. Folding stops at
the first non-constant argument to preserve dunder dispatch semantics.

```koskari
(+ 1 2 a)   ; → CONST 3, VAR a, OBJECT_ADD 2
(+ 1 2)     ; → CONST 3, OBJECT_ADD 1
(+ a 1 2)   ; → VAR a, CONST 1, CONST 2, OBJECT_ADD 3  (no fold)
```

`compile/**`, `compile/instance?`, and `compile/subclass?` never fold
(non-trivial multi-argument semantics).

## Comparison operators

All comparison specials require at least two arguments and emit a COUNT opcode
with the number of operands. Chained comparisons follow Python semantics (each
adjacent pair must satisfy the relation).

| Surface | Aliases | Opcode | Chained semantics |
| ------- | ------- | ------ | ----------------- |
| `==` | `equal?` | `OBJECT_EQ` | `A == B == ...` |
| `!=` | `not-equal?` | `OBJECT_NE` | `A != B != ...` |
| `<` | `lt?` | `OBJECT_LT` | `A < B < ...` |
| `<=` | `le?` | `OBJECT_LE` | `A <= B <= ...` |
| `>` | `gt?` | `OBJECT_GT` | `A > B > ...` |
| `>=` | `ge?` | `OBJECT_GE` | `A >= B >= ...` |

```koskari
(< 0 1 2 3)      ; true
(<= 1 2 2 4)     ; true
(== 5 5 7 5)     ; false
```

## Type predicates

`(instance? OBJ TYPE)` checks whether `OBJ` is an instance of `TYPE`.
With several types, the check succeeds if any type matches (tuple semantics
on the VM side).

`(subclass? CLS BASE)` checks subclass relationship; multiple bases are
combined the same way.

```koskari
(instance? 42 int)
(subclass? list object)
```

## Exponentiation and formatting

`( ** BASE EXP [MOD])` calls `__pow__`. Constant folding does not apply.

`(format OBJ)` and `(format OBJ RULES)` call Python `format`. `RULES` may be
a format string or a quoted symbol naming a format style.

```koskari
(format 42)
(format 42 "{:x}")
```

## Further operators

Unary and binary numeric operators (`+`, `-`, `*`, `/`, bitwise forms, `not`,
`repr`, `len`, `type`, and others) share the same compilation pipeline. See the
per-form pages under [Special forms](../index.md) for symbols that have
dedicated write-ups.
