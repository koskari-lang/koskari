# References and storage indirection

A **ref** is a safe handle to a *storage location* — the box a name labels,
rather than the value inside it. If you have used pointers in C, a ref is the
same idea with the sharp edges removed: it aims at exactly one location, has no
arithmetic, and can never be null or dangling.

The shape at a glance:

```koskari
(define food 10)
(define r &food)          ; &x is sugar for (ref food): the location, not the value
(deref r)                 ; -> 10   read through the ref
(set! (deref r) 20)       ; write through the ref; food is now 20
```

`&` always means *the location, not the value* — on a variable (`&food`), on a
slot with no name (`&(item xs 2)`, `&obj.count`), and on a function parameter
(`define bump [&n]: ...`), where it lets a callee write back into the caller's
variable. `bind!` makes the indirection invisible: a name bound to a ref reads
and writes through automatically.

**Is this for you?** Reach for refs when you need a function to mutate a
caller's binding, or to hand around a writable handle to a list slot or
attribute. If you only pass and return values, you can skip this and come back
later.

**Go deeper:** [References and storage
indirection](../reference/concepts/references-and-storage-indirection.md) is the
authoritative walkthrough — every case worked end to end, the `set!` target
trap, the by-ref guardrail, and how this differs from Python. See also
[Bindings and refs](../reference/concepts/bindings-and-refs.md) for the precise
vocabulary.
