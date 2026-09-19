# Bindings and environments

A **binding** connects a name to a value. An **environment** is the place where
Koskari looks up those names.

---

## Defining names

Use `define` to introduce a name:

```koskari
(define greeting "hello")
(define count 3)
(print greeting count)
```

At the top level of a file, the binding belongs to that module. Inside a
function or `let`, the binding belongs to a smaller local environment.

---

## Local names with `let`

Use `let` when helper names should exist only for one calculation:

```koskari
(let [[width 6] [height 7]]
  (* width height))
```

Outside the `let` body, `width` and `height` are not introduced by that `let`.
That makes local calculations safer to change.

Layout form is often easier to read for several lines:

```koskari
let [[name "Ada"] [points 42]]:
  (print name points)
```

---

## Changing an existing binding

Use `set!` when you intentionally update a name that already exists:

```koskari
(define counter 0)
(set! counter (+ counter 1))
(print counter)
```

Prefer `define` for introducing names and `set!` for deliberate updates. That
habit makes code easier to audit.

---

## Shadowing

A local binding can use the same name as an outer binding. The inner one wins
inside its body:

```koskari
(define message "outer")
(let [[message "inner"]]
  (print message))
(print message)
```

This is called **shadowing**. It is useful in small scopes, but confusing when
used casually.

---

## What you should remember

Names live in environments. `define` introduces names, `let` creates local
bindings, and `set!` updates an existing binding. Use small scopes and clear
names to keep programs friendly.

---

**Go deeper:** [Bindings and refs](bindings-and-refs.md)
— the precise namespace / bind-vs-store / deref vocabulary.
[References and storage indirection](references-and-storage-indirection.md)
— getting a handle to a storage location and writing through it.
