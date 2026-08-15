# Idiomatic Koskari

Koskari gives you direct, named forms for common operations. Code ported
mechanically from Python often reaches those operations by assembling them from
primitives, which stays correct but reads as a translation. This page collects
the cases where a native form says the same thing more plainly. Every pair below
is behavior-equivalent; the right-hand form is the idiomatic spelling.

The single rule: **prefer the direct form over the same result built out of
primitives.**

## Slicing

`item-slice` slices a sequence directly. Building a `slice` object and handing
it to `item` is the assembled-from-primitives version.

```koskari
(item seq (slice 1 None))    ; verbose
(item-slice seq 1)           ; idiomatic

(item seq (slice None n))    ; verbose
(item-slice seq stop: n)     ; idiomatic -- name the bound
```

## Attribute assignment

A dotted target on `set!` reads as an attribute location, so a separate
`set-attr` call is unnecessary.

```koskari
(set-attr obj value 7)   ; verbose
(set! obj.value 7)       ; idiomatic
```

The dotted form reads as `(set! (attr obj value) 7)`.

## Destructuring

When a form returns a sequence, bind its parts with `define-values` rather than
indexing each position by hand.

```koskari
; verbose
define fn (item pair 0)
define args (item pair 1)

; idiomatic
define-values [fn args] pair
```

`define-values` also takes a star-rest, which replaces a head-plus-slice pair:

```koskari
; verbose
define name (item argv 0)
define rest (item argv (slice 1 None))

; idiomatic
define-values [name *: rest] argv
```

## Testing for None

Koskari has a named predicate for the `None` singleton. Prefer it over an
inline `is` / `is-not` comparison.

```koskari
(is x None)       ; verbose
(None? x)         ; idiomatic

(is-not x None)   ; verbose
(not (None? x))   ; idiomatic
```

For a single-branch guard, keep the predicate positive and let the control form
carry the polarity. `unless` and `when` exist so you do not have to negate the
test.

```koskari
; verbose
if (is-not x None):
  handle x

; idiomatic
unless (None? x):
  handle x
```

`when (None? x):` is the mirror for the "is None" guard. Reserve `if` for
genuine two-branch conditionals, swapping the arms to keep `None?` positive when
that reads more clearly.

For what `None?` actually tests -- identity against `None`, not truthiness --
see [compile/None? and compile/nil?](../macros/compile-none-and-compile-nil.md).
