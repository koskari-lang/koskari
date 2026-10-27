# Types and type predicates

Every type below is bound under its name in every module, and a
predicate named after it with a `?` suffix is bound beside it. The type
is callable as a constructor and usable with `instance?`.

## Type predicates

**Syntax:** `(NAME? VALUE)`

Return `True` when **`VALUE`** is an instance of the type bound as
**`NAME`**, `False` otherwise. `(NAME? x)` is `(instance? x NAME)`, so
an instance of a subclass passes.

```koskari
(tuple? #(1 2))        ; True
(tuple? #[1 2])        ; False
(list? #[1 2])         ; True
(list? '(1 2))         ; False -- a quoted list is a pair chain
(str? "abc")           ; True
(str? 'abc)            ; False -- a symbol
(int? True)            ; True -- bool is a subclass of int
(bool? 1)              ; False
```

A predicate is an ordinary one-argument function:

```koskari
(list (filter str? #[1 "a" 2 "b"]))    ; #["a" "b"]
(list (map int? #[1 "a" True]))        ; #[True False True]
```

Defining a class binds only the class name. No predicate is created
for it; test with `instance?`.

## Numbers

| Name | Type |
|---|---|
| `bool` | `True` and `False` |
| `int` | Integers |
| `float` | Floating point numbers |
| `complex` | Complex numbers |
| `decimal` | Decimal fixed point numbers, `decimal.Decimal` |
| `fraction` | Rational numbers, `fractions.Fraction` |

**Also:** macro [`bool`](../macros/compile-bool.md).

```koskari
(float? 7)                       ; False
(decimal? (decimal "1.5"))       ; True
(decimal? 1.5)                   ; False
(fraction? (fraction 1 3))       ; True
(str (+ (fraction 1 3) (fraction 1 6)))   ; "1/2"
```

## Text and bytes

| Name | Type |
|---|---|
| `str` | Strings |
| `bytes` | Immutable byte strings |
| `bytearray` | Mutable byte strings |
| `memoryview` | Views over a byte buffer |

```koskari
(bytes? (bytes 3))               ; True
(bytes? (bytearray 3))           ; False
```

## Collections

| Name | Type |
|---|---|
| `list` | Lists, `#[...]` |
| `tuple` | Tuples, `#(...)` |
| `dict` | Dicts, `#{...}` |
| `set` | Mutable sets |
| `frozenset` | Immutable sets |
| `range` | Integer ranges |
| `slice` | Slice objects |

`list`, `tuple`, `set`, and `frozenset` take one iterable; the `#`
builders on [Functions](functions.md#collection-builders) take the
elements as arguments.

**Also:** macro [`slice`](../macros/compile-slice-and-compile-item-slice.md).

```koskari
(set? (set #[1 2]))              ; True
(set? (frozenset #[1]))          ; False
(sorted (frozenset #[3 1 3]))    ; #[1 3]
(dict? #{"a": 1})                ; True
```

## Objects and classes

| Name | Type |
|---|---|
| `object` | The base of every value |
| `type` | Classes |
| `property` | Properties; accepts Koskari accessor functions directly |
| `classmethod` | Class method wrappers |
| `staticmethod` | Static method wrappers |

**Also:** special form
[`type`](../specials/object-operators/index.md#unary-and-inspection-operators).

```koskari
(type? int)                      ; True
(type? 5)                        ; False
(object? None)                   ; True -- every value is an object
(is? (type 5) int)               ; True
```

## Pairs and symbols

| Name | Type |
|---|---|
| `pair` | Cons cells, and `nil`, the empty list |
| `symbol` | Symbols, such as `'abc` |
| `keyword` | Keywords, such as `'abc:` |
| `datum` | Values wrapped with a source position |

`nil` passes `pair?`. Use `nil?` to test for the empty list, and test
it before `pair?` when the two cases differ.

**Also:** macro [`datum`](../macros/compile-datum.md).

```koskari
(pair? '(1 2))             ; True
(pair? nil)                ; True -- nil is the empty pair
(pair? #[1 2])             ; False
(symbol? 'abc)             ; True
(keyword? 'abc:)           ; True
(keyword? 'abc)            ; False
```

## Functions and results

| Name | Values |
|---|---|
| `partial` | Partial applications, from `(partial FUNC ARG ...)` |
| `demand` | Callables that run their function on the demand path |
| `constantly` | Callables that always return one value |
| `values` | Multiple results, from `(values ...)` |
| `defer` | Single-shot held work, from `(defer THUNK)` |

See [Functions](functions.md#calling) for each constructor.

```koskari
(partial? (partial + 1))   ; True
(partial? +)               ; False
(values? (values 1 2))     ; True
```

## Iterators

| Name | Type |
|---|---|
| `map`, `filter`, `zip` | Lazy iterators that accept Koskari functions; see [map and filter](map-and-filter.md) |
| `py-map`, `py-filter`, `py-zip` | Python's `map`, `filter`, and `zip` |

The two families are distinct types, and each predicate accepts only
its own:

```koskari
(map? (map str #[1]))           ; True
(map? (py-map str #[1]))        ; False
(py-map? (py-map str #[1]))     ; True
(py-zip? (zip #[1] #[2]))       ; False
```

## Refs

| Name | Ref |
|---|---|
| `Ref` | Every ref; the base of the kinds below |
| `ItemRef` | A subscript, from `(ref (item OBJ KEY))` |
| `AttributeRef` | An attribute, from `(ref (attr OBJ NAME))` |
| `DynamicRef` | Getter and setter functions, from `(ref (dynamic GETFN ...))` |
| `ConstantRef` | A value that refuses stores, from `(ref (constant VALUE))` |
| `MemoizedRef` | A getter called once on first deref, from `(ref (memoized GETTER))` |
| `ParamRef` | A parameter, from `(ParamRef 'NAME)` |

`ref?` is the same predicate as `Ref?`. See [`ref`](../macros/compile-ref.md).

```koskari
var xs #[1 2 3]
(ref? &xs)                         ; True
(ItemRef? (ref (item xs 0)))       ; True
(ConstantRef? (ref (constant 5)))  ; True
(ref? 5)                           ; False
```

## Processes

`ProcessResult` is the result of `run-process`; see
[Functions](functions.md#async). Its predicate is `ProcessResult?`.

## Exceptions

Every exception type is bound by name, Koskari's and Python's. The
Koskari exception classes and their hierarchy are on the
[Error guide](../error-codes.md).
