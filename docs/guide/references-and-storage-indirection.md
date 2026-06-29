# References and storage indirection

This chapter assumes you are comfortable with `define` and `set!` — you know
that defining a name lets you refer to it and assign new values into it. We
build on that one idea: that a name is attached to a **storage location**, and
that you can get hold of that location itself. Koskari calls such a handle a
**ref**.

If you have met pointers in C, a ref will feel familiar — but a ref is *safer*:
it points at exactly one storage location, you cannot do arithmetic on it, and
there is no null or dangling ref to trip over.

Every snippet here is exercised in [examples/refs.ki](https://github.com/obriencj/python-koskari/blob/master/examples/refs.ki).

---

## A name labels a storage location

> **Q:** When you write `(set! food 11)`, what actually changes?
>
> **A:** Not the name `food` — the **storage location** it labels. `define`
> creates the location and attaches the name; `set!` changes what is *inside*
> it.
>
> ```koskari
> (define food 10)
> (set! food 11)
> food
> ; -> 11
> ```

> **Q:** So a name is like a label on a box. Can you get hold of the box
> itself, instead of what is inside it?
>
> **A:** Yes. That handle to the box is a **ref**.

---

## Getting a ref: the long form, then the `&` sugar

> **Q:** How do you get a ref to the location behind `food`?
>
> **A:** `(ref food)`:
>
> ```koskari
> (define r (ref food))
> ```

> **Q:** Is there a shorter way?
>
> **A:** Yes — `&food` is sugar for `(ref food)`. The two are identical:
>
> ```koskari
> (define r &food)
> (type r)
> ; -> <class 'Ref'>
> ```
>
> You will see `&` again later, on function parameters. It always means the
> same thing: *the location, not the value.*

---

## A ref is an ordinary value

> **Q:** `r` holds a ref. If you ask for `r`, do you get `11` back?
>
> **A:** No — you get the **ref object** itself. A ref is a value you can pass
> around and store like any other. To read the value it points at, **deref**
> it:
>
> ```koskari
> (deref r)
> ; -> 11
> ```
>
> This is the first half of the indirection: `r` is one step removed from the
> value. `food` gives you the value directly; `r` gives you the location, and
> `(deref r)` follows it.

---

## Writing through a ref

> **Q:** A ref points at `food`'s location. Can you change `food` *without
> naming it* — through the ref alone?
>
> **A:** Yes. `store` writes through the ref to the location it points at:
>
> ```koskari
> (r.store 20)
> food
> ; -> 20
> ```
>
> Look closely: the line that changed `food` never mentioned `food`. That is
> storage indirection at work — `r` *is* the box, so writing through `r` writes
> into `food`.

> **Q:** Then to change `food` through `r`, would you write `(set! r 20)`?
>
> **A:** Predict before reading on... No — and this is the one thing to get
> straight. `set!` on a **name** changes what *that name* holds. `r` holds a
> ref; `(set! r 20)` throws the ref away and puts `20` in `r` instead:
>
> ```koskari
> (set! r 99)
> food
> ; -> 20    (unchanged!)
> r
> ; -> 99    (r no longer holds a ref)
> ```
>
> So: `(r.store v)` writes *through* the ref; `(set! r v)` *replaces* the ref.
> When a ref is just a value you are holding, you reach the target with `deref`
> and `store`, never with `set!` on the holding name.

---

## Refs to locations that have no name

> **Q:** Variables have names. What about a slot inside a list, or an attribute
> on an object — can you get a ref to one of those?
>
> **A:** Yes. `&(item xs 2)` is a ref to the third element's location;
> `&obj.count` is a ref to that attribute's location:
>
> ```koskari
> (define xs #[1 2 3 4])
> (define cell &(item xs 2))
> (deref cell)
> ; -> 3
> (cell.store 100)
> xs
> ; -> [1, 2, 100, 4]
> ```
>
> Now "storage location" earns its name: `cell` points at a slot that has no
> variable name of its own, yet you can read and write it through the ref just
> the same.

---

## Making the indirection invisible: `bind!`

> **Q:** Carrying a ref around and remembering to `deref` / `store` it is a
> chore. Can a name's location simply *be* a ref, so you read and write it like
> any other variable?
>
> **A:** That is exactly what `bind!` does. It attaches a name to a ref **as
> that name's storage**:
>
> ```koskari
> (define total 5)
> (bind! alias &total)
> alias
> ; -> 5            (reads through automatically)
> (set! alias 42)
> total
> ; -> 42           (set! writes through automatically)
> ```
>
> Once bound, the indirection is invisible: plain `alias` derefs for you, and
> plain `set! alias` stores through for you. No `deref`, no `store`.

> **Q:** Does that work for the no-name locations too?
>
> **A:** Yes — bind a name to an item or attribute ref and then just use the
> name:
>
> ```koskari
> (bind! first &(item xs 0))
> (set! first 1000)
> xs
> ; -> [1000, 2, 100, 4]
> ```
>
> This is the "safer pointer" payoff. You aim a name at a location once, and
> from then on the code reads like ordinary variable access while quietly
> writing back to the original slot.

---

## Passing a ref to a function — by hand first

> **Q:** A ref is just a value, so you can pass one into a function. How would a
> function use it to change the caller's variable?
>
> **A:** Take the ref as an ordinary parameter, then `bind!` a local name to it
> — the same move as above, now inside the function:
>
> ```koskari
> define bump-manual [the-ref]:
>   bind! n the-ref
>   set! n (+ n 1)
>
> (define a 7)
> (bump-manual &a)
> a
> ; -> 8
> ```
>
> The caller passes `&a` (the location of `a`); the function binds `n` to that
> location, so `(set! n ...)` writes back into `a`.

---

## By-ref parameters do the `bind!` for you

> **Q:** Binding the parameter by hand every time is boilerplate. Is there
> sugar?
>
> **A:** Mark the parameter itself with `&`. A by-ref parameter binds the
> caller's ref as its storage automatically — the function body just uses the
> name:
>
> ```koskari
> define bump [&n]:
>   set! n (+ n 1)
>
> (define b 7)
> (bump &b)
> b
> ; -> 8
> ```
>
> Note the two `&`, doing the matching halves of one handshake: `&n` in the
> parameter list says "I want the location," and `&b` at the call says "here is
> the location." This is the third time `&` has meant *location, not value* —
> on `(ref x)`, on items and attributes, and now on parameters.

---

## The guardrail

> **Q:** What if you forget the `&` and pass a plain value to a by-ref
> parameter?
>
> **A:** It is an error, caught immediately — not a silent corruption:
>
> ```koskari
> (bump 5)
> ; -> TypeError: bump() by-ref formal n requires a Ref, not 5
> ```
>
> This is part of what makes refs safer than raw pointers: a by-ref slot only
> accepts an actual ref, aimed at exactly one location.

---

## How this differs from Python

> **Q:** Python passes everything "by reference" already, doesn't it?
>
> **A:** Python names already reference *objects*, so two names can see the same
> mutable list. But you cannot get a reference to a **binding** — a callee can
> never rebind a caller's variable. Koskari lets you pass the *location itself*,
> so a function can write back to the caller's `a`, a list slot, or an
> attribute. Python passes object references by value; Koskari can pass the slot.

---

## Where next

- [Bindings and refs](../reference/concepts/bindings-and-refs.md) — the precise
  vocabulary (bind vs store, deref, namespaces).
- [`compile/ref`](../reference/macros/compile-ref.md) and the by-ref formals
  note under [`compile/lambda`](../reference/specials/compile-lambda.md).
- Refs whose get/set is *computed* (dynamic refs) push storage indirection one
  step further — there is no underlying slot at all, just behavior. They are a
  topic for a future advanced recipe.
