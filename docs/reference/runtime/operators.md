# Operators

The arithmetic, comparison, and membership operators are bound as
functions, so they can be passed to `apply`, `sorted`,
`functools.reduce`, and other higher-order functions. In head position
the same names compile inline; see
[Object operator specials](../specials/object-operators/index.md).

```koskari
(apply + #[1 2 3])             ; 6
(apply < #[0 1 2 3])           ; True
```

## Arithmetic and bitwise

| Name | Arguments | Does |
|---|---|---|
| `+` | one or more | Sum, left to right; one argument is unary plus |
| `-` | one or more | Difference, left to right; one argument is negation |
| `*` | two or more | Product, left to right |
| `/` | two or more | True division, left to right |
| `//` | two or more | Floor division, left to right |
| `**` | two or three | Power; a third argument is the modulus, as in Python's `pow` |
| `@` | two or more | Matrix multiplication through `__matmul__` |
| `&` | two or more | Bitwise and |
| `\|` | two or more | Bitwise or |
| `^` | two or more | Bitwise exclusive or |
| `<<` | two | Left shift |
| `>>` | two | Right shift |

The operators dispatch through the operands' dunder methods, so `+`
concatenates strings and lists. A call with too few arguments raises
`TypeError`.

**Also:** special forms, [arithmetic and bitwise](../specials/object-operators/index.md#arithmetic-and-bitwise-operators).

```koskari
(apply - #[10 3 2])            ; 5
(apply - #[5])                 ; -5
(apply / #[12 2 3])            ; 2.0
(apply ** #[2 10 1000])        ; 24
(apply | #[6 3 8])             ; 15
(apply << #[1 4])              ; 16
(+ "a" "b" "c")                ; "abc"
```

## Comparison

| Name | Alias | Does |
|---|---|---|
| `==` | `equal?` | Equal |
| `!=` | `not-equal?` | Not equal |
| `<` | `lt?` | Less than |
| `<=` | `le?` | Less than or equal |
| `>` | `gt?` | Greater than |
| `>=` | `ge?` | Greater than or equal |

Each takes two or more arguments and chains as Python does: every
adjacent pair must satisfy the relation.

**Also:** special forms, [comparison](../specials/object-operators/index.md#comparison-operators).

```koskari
(apply == #[1 1 1])            ; True
(apply lt? #[3 2])             ; False
```

## Membership and identity

| Name | Alias | Does |
|---|---|---|
| `in` | `in?` | `(in ITEM CONTAINER)`, membership through `__contains__` |
| `not-in` | `not-in?` | Negated membership |
| `is?` | | `(is? A B)`, identity |
| `is-not?` | | Negated identity |

**Also:** special forms, [membership and identity](../specials/object-operators/index.md#membership-and-identity).

```koskari
(apply in? #[2 #[1 2]])        ; True
(apply is? #[None None])       ; True
```

`instance?` and `subclass?` are on [Functions](functions.md#predicates).

## Compile-only operators

`%`, `~`, `divmod`, `not`, `truth`, `is`, and `is-not` exist only as
special forms. They have no runtime binding and cannot be passed as
values; wrap one in a lambda, or use `is?` and `is-not?` for identity.

```koskari
(list (map (lambda [x] (% x 3)) #[4 5 6]))    ; #[1 2 0]
```
