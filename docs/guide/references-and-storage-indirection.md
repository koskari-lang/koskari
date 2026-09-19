# References and storage indirection

A name is attached to a storage location, and the location itself can be
taken hold of. Koskari calls that handle a ref. A ref is a pointer with
the sharp edges removed: it points at one storage location, there is no
arithmetic on it, and it is never null or dangling.

[Bindings and refs](bindings-and-refs.md) is the vocabulary page -- bind
against store, the ref surface forms, by-ref formals. This page is about
what the indirection buys you.

The snippets here are also `examples/refs.ki` in the repository.

## A name labels a location

`define` creates a storage location and attaches a name to it. `set!`
changes what is inside the location; the name does not move.

```koskari
(define food 10)
(set! food 11)
food
; -> 11
```

`(ref food)`, or its reader shorthand `&food`, hands you the location
rather than its contents:

```koskari
(define r &food)
(type r)
; -> <class 'koskari.lib.Ref'>
(deref r)
; -> 11
```

`r` is one step removed from the value. `food` gives the value directly;
`r` gives the location, and `(deref r)` follows it.

## Reading and writing through a ref

Assigning to the deref stores through the ref into the location it points
at:

```koskari
(set! (deref r) 20)
food
; -> 20
```

The line that changed `food` never mentioned `food`. Under the hood this
dispatches to the ref's `store` method, so `(r.store 20)` does the same
thing when you already hold the ref.

The target of `set!` decides which of the two happens. `(set! (deref r)
v)` writes through the ref; `(set! r v)` assigns to the name `r`,
discarding the ref:

```koskari
(set! r 99)
food
; -> 20    (unchanged)
r
; -> 99    (r no longer holds a ref)
```

## Locations with no name

A ref can point at a list slot or an object attribute, neither of which
has a variable name:

```koskari
(define xs #[1 2 3 4])
(define cell &(item xs 2))
(deref cell)
; -> 3
(set! (deref cell) 100)
xs
; -> [1, 2, 100, 4]
```

`&obj.count` is the same move on an attribute. This is where "storage
location" earns its name: `cell` reads and writes a slot that no name
labels.

## bind! makes the indirection invisible

`bind!` attaches a name to a ref as that name's storage, so plain reads
deref and plain `set!` stores through:

```koskari
(define total 5)
(bind! alias &total)
alias
; -> 5
(set! alias 42)
total
; -> 42
```

That works for the unnamed locations too, which is the payoff: aim a name
at a slot once, and from then on the code reads like ordinary variable
access while writing back to the original slot.

```koskari
(bind! head &(item xs 0))
(set! head 1000)
xs
; -> [1000, 2, 100, 4]
```

A by-ref formal, `[&n]`, does this `bind!` for the caller's ref
automatically; see
[by-ref formals](bindings-and-refs.md#by-ref-formals). Passing a
non-ref to one is an error at the call, not a silent corruption:

```koskari
define bump [&n]:
  set! n (+ n 1)

(bump 5)
; -> TypeError: bump() by-ref formal n requires a Ref, not 5
```

## take!

[`take!`](../reference/macros/compile-take.md) reads a location and
clears it in one step, returning the value that was there:

```koskari
(define y 5)
(take! y)          ;; 5; y is now empty
(define d #{"j": 5})
(take! d["j"])     ;; 5
d                  ;; {}
```

Reading a cleared name afterwards raises
`NameError: Empty reference: y`. Use `take!` when
handing ownership of a value onward, so nothing reads the old location by
accident.

## Dynamic refs

The indirection goes one step further than a slot. A dynamic ref supplies
its own get, set, and clear behaviour, so there is no underlying storage
at all -- reading the name runs a function:

```koskari
(define store #{"temp": 0})
(define log #[])

(bind! sensor (dynamic { (begin (log.append "read") store["temp"]) }
                       { (set! store["temp"] %1) }))

sensor        ; -> 0        (runs the getter)
(set! sensor 7)
store         ; -> {'temp': 7}
log           ; -> ['read']
```

The two fences are the getter and the setter; the setter receives the new
value as `%1`. Optional third and fourth fences supply the clear and the
emptiness test. `bind!` takes the same position for `constant`,
`memoized`, `parameter`, and `environ` refs, which each compute their
value rather than hold it.

## Compared with Python

Python names already reference objects, so two names can see the same
mutable list. You cannot get a reference to a binding, though, so a callee
can never rebind a caller's variable. Koskari lets you pass the location
itself, so a function can write back to the caller's `a`, a list slot, or
an attribute. Python passes object references by value; Koskari can pass
the slot.

## See also

- [Bindings and refs](bindings-and-refs.md): bind against store, deref,
  namespaces, by-ref formals
- [`compile/ref`](../reference/macros/compile-ref.md) and
  [`compile/lambda`](../reference/specials/compile-lambda.md)
- [`compile/take!`](../reference/macros/compile-take.md)
