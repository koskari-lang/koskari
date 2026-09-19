# Idiomatic Koskari

Koskari has a direct, named form for each common operation. Code ported
mechanically from Python reaches the same operations by assembling
primitives, which is correct and reads as a translation. Each pair
below is behaviour-equivalent, and the second spelling is the idiom.

The rule: prefer the direct form over the same result built from
primitives.

## Slicing

`item-slice` slices a sequence. Building a `slice` object and handing it
to `item` is the assembled version.

```koskari
(item seq (slice 1 None))    ; assembled
(item-slice seq 1)           ; idiom

(item seq (slice None n))    ; assembled
(item-slice seq stop: n)     ; idiom, naming the bound
```

## Attribute assignment

A dotted target on `set!` is an attribute location, so `set-attr` is
not needed.

```koskari
(set-attr obj value 7)   ; assembled
(set! obj.value 7)       ; idiom
```

The dotted form reads as `(set! (attr obj value) 7)`.

## Destructuring

When a form returns a sequence, bind its parts with `define-values`
rather than indexing each position.

```koskari
; assembled
define fn (item pair 0)
define args (item pair 1)

; idiom
define-values [fn args] pair
```

`define-values` takes a `*:` rest, which replaces a head plus a slice:

```koskari
; assembled
define name (item argv 0)
define rest (item argv (slice 1 None))

; idiom
define-values [name *: rest] argv
```

## Testing for None

`None?` is the predicate for the `None` singleton.

```koskari
(is x None)       ; assembled
(None? x)         ; idiom

(is-not x None)   ; assembled
(not (None? x))   ; idiom
```

For a one-armed guard, keep the predicate positive and let the control
form carry the polarity. `unless` and `when` exist so the test is not
negated.

```koskari
; assembled
if (is-not x None):
  handle x

; idiom
unless (None? x):
  handle x
```

`when (None? x):` is the guard for the other case. `if` is for a
two-armed choice, with the arms swapped when that keeps `None?`
positive.

`None?` tests identity against `None`, not truthiness; see
[`None?` and `nil?`](../reference/macros/compile-none-and-compile-nil.md).
