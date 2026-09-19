# Object operator specials

The operator specials are the forms the compiler emits as a single object
operation rather than a call. Most map to an `OBJECT_*` opcode; the collection
builders at the end of the page emit the `BUILD_*` family instead.

## Constant folding

Reducible (COUNT) operators apply left-to-right constant folding: consecutive
leading arguments that compile to a single `CONST` are folded together using
the corresponding Python operator before any code is emitted. Folding stops at
the first non-constant argument, which preserves dunder dispatch.

```koskari
(+ 1 2 a)   ; -> CONST 3, VAR a, OBJECT_OPERAND (ADD) 2
(+ 1 2)     ; -> CONST 3
(+ a 1 2)   ; -> VAR a, CONST 1, CONST 2, OBJECT_OPERAND (ADD) 3  (no fold)
```

`**`, `instance?`, and `subclass?` never fold.

## Comparison operators

All comparison specials require at least two arguments and emit a COUNT opcode
with the number of operands. Chained comparisons follow Python semantics: each
adjacent pair must satisfy the relation.

| Surface | Alias | Opcode | Chained semantics | Example |
| ------- | ----- | ------ | ----------------- | ------- |
| `==` | `equal?` | `OBJECT_COMPARE` (EQ) | `A == B == ...` | `(== 1 1 1)` -> `True` |
| `!=` | `not-equal?` | `OBJECT_COMPARE` (NE) | `A != B != ...` | `(!= 1 2)` -> `True` |
| `<` | `lt?` | `OBJECT_COMPARE` (LT) | `A < B < ...` | `(< 0 1 2 3)` -> `True` |
| `<=` | `le?` | `OBJECT_COMPARE` (LE) | `A <= B <= ...` | `(<= 1 2 2 4)` -> `True` |
| `>` | `gt?` | `OBJECT_COMPARE` (GT) | `A > B > ...` | `(> 3 2)` -> `True` |
| `>=` | `ge?` | `OBJECT_COMPARE` (GE) | `A >= B >= ...` | `(>= 2 2)` -> `True` |

```koskari
>>> (< 0 1 2 3)
True
>>> (<= 1 2 2 4)
True
>>> (== 5 5 7 5)
False
```

## Membership and identity

These take exactly two operands and also emit `OBJECT_COMPARE`. Each has a
`?`-suffixed alias.

| Form | Alias | Does | Example |
| ---- | ----- | ---- | ------- |
| `in` | `in?` | Membership through `__contains__` | `(in 2 #[1 2 3])` -> `True` |
| `not-in` | `not-in?` | Negated membership | `(not-in 5 #[1 2 3])` -> `True` |
| `is` | `is?` | Identity | `(is None None)` -> `True` |
| `is-not` | `is-not?` | Negated identity | `(is-not 1 2)` -> `True` |

## Type predicates

`(instance? OBJ TYPE ...)` checks whether OBJ is an instance of any of the
types given. `(subclass? CLS BASE ...)` checks the subclass relation, combining
several bases the same way.

```koskari
>>> (instance? 42 int)
True
>>> (subclass? list object)
True
```

## Arithmetic and bitwise operators

Each emits `OBJECT_OPERAND` with the matching selector. The reducible forms
take any number of operands and fold left to right.

| Form | Does | Example |
| ---- | ---- | ------- |
| `+` | Addition; one operand is unary plus | `(+ 1 2 3)` -> `6` |
| `-` | Subtraction; one operand is negation | `(- 10 3)` -> `7` |
| `*` | Multiplication | `(* 2 3 4)` -> `24` |
| `/` | True division | `(/ 7 2)` -> `3.5` |
| `//` | Floor division | `(// 7 2)` -> `3` |
| `%` | Remainder | `(% 7 2)` -> `1` |
| `**` | Exponentiation, with an optional modulus | `(** 2 10)` -> `1024` |
| `@` | Matrix multiplication through `__matmul__` | `(@ a b)` |
| `&` | Bitwise and | `(& 6 3)` -> `2` |
| `\|` | Bitwise or | `(\| 6 3)` -> `7` |
| `^` | Bitwise exclusive or | `(^ 6 3)` -> `5` |
| `~` | Bitwise inversion, one operand | `(~ 6)` -> `-7` |
| `<<` | Left shift | `(<< 1 4)` -> `16` |
| `>>` | Right shift | `(>> 16 2)` -> `4` |
| `divmod` | Quotient and remainder as a tuple | `(divmod 17 5)` -> `#(3 2)` |

`**` takes a third operand as the modulus:

```koskari
>>> (** 2 10 7)
2
```

## Unary and inspection operators

| Form | Opcode | Does | Example |
| ---- | ------ | ---- | ------- |
| `not` | `OBJECT_NOT` | Logical negation of truthiness | `(not 0)` -> `True` |
| `truth` | `OBJECT_TRUTH` | The operand's truth value as a bool | `(truth #[])` -> `False` |
| `nil?` | `OBJECT_COMPARE` (IS) | Identity against `nil` | `(nil? nil)` -> `True` |
| `repr` | `OBJECT_REPR` | The `__repr__` string | `(repr "hi")` -> `"\"hi\""` |
| `len` | `OBJECT_LEN` | Length through `__len__` | `(len #[1 2 3])` -> `3` |
| `type` | `OBJECT_TYPE` | The operand's type | `(type 42)` -> `<class 'int'>` |
| `dir` | `OBJECT_DIR` | The attribute name list | `(in "append" (dir #[]))` -> `True` |
| `vars` | `OBJECT_VARS` | The instance `__dict__` | `(vars obj)` -> `#{"x": 1}` |
| `iter` | `OBJECT_ITER` | An iterator over the operand | `(iter #[1 2 3])` |
| `next` | `OBJECT_NEXT` | The next value from an iterator, with an optional default | `(next it)` -> `1` |

`nil?` is a special, unlike `None?`, which is a
[macro](../../macros/compile-none-and-compile-nil.md).

## String conversion and formatting

| Form | Opcode | Does | Example |
| ---- | ------ | ---- | ------- |
| `#str` | `OBJECT_STR` | Variadic `__str__` conversion, concatenated | `(#str 1 2 3)` -> `"123"` |
| `format` | `OBJECT_FORMAT` | Python `format` with an optional spec | `(format 42)` -> `"42"` |

The second argument to `format` is a bare format spec -- the text that would
follow the colon in a Python format string, not a `{...}` template:

```koskari
>>> (format 42 "x")
"2a"
>>> (format 42)
"42"
```

See [`compile/#str`](../compile-str.md) and
[`compile/format`](../compile-format.md).

## Collection builders

These emit the `BUILD_*` opcodes rather than an `OBJECT_*` one. A literal whose
elements are all constants reduces to a single instruction.

| Form | Does | Example |
| ---- | ---- | ------- |
| `#list` | Build a list; what `#[...]` reads to | `(#list 1 2 3)` -> `#[1 2 3]` |
| `#tuple` | Build a tuple; what `#(...)` reads to | `(#tuple 1 2 3)` -> `#(1 2 3)` |
| `#list-append` | Append to a list binding or list expression | `(#list-append lst 3)` |

```koskari
>>> define lst #[1 2]
>>> (#list-append lst 3)
>>> lst
#[1 2 3]
```

## In-place update

| Form | Does |
| ---- | ---- |
| [`inplace`](../compile-inplace.md) | Update a variable in place; the special under `incr!` and `decr!` |
| [`inplace-item`](../compile-inplace-item.md) | Update a subscript in place |
| [`inplace-attr`](../compile-inplace-attr.md) | Update an attribute in place |
