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

When a form returns a sequence, bind its parts with `var` over a
pattern rather than indexing each position.

```koskari
; assembled
var fn (item pair 0)
var args (item pair 1)

; idiom
var [fn args] pair
```

The pattern takes a `*:` rest, which replaces a head plus a slice:

```koskari
; assembled
var name (item argv 0)
var rest (item argv (slice 1 None))

; idiom
var [name *: rest] argv
```

These two differ in the type of the tail. Slicing a list yields a list,
while the `*:` collector always yields a tuple, so `rest` is `#["b" "c"]`
from the slice and `#("b" "c")` from the collector. Use the collector
when the tail is iterated or unpacked, and the slice when the caller
needs a list.

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
