# Functions and recursion

Functions turn a repeated calculation into something you can name, call, test,
and explain.

---

## Function shorthand

The common layout form is:

```koskari
define double [n]:
  (* n 2)
```

The parenthesized form is equivalent in spirit:

```koskari
(define double [n]
  (* n 2))
```

Call the function by putting its name first:

```koskari
(double 21)
```

---

## Body values

A function body runs in order. The last form supplies the result unless you use
an explicit `return` for early exit:

```koskari
define shipping [subtotal]:
  if (>= subtotal 50):
    0
  else:
    5
```

---

## Defaults and keywords

Function formals can include keyword-style defaults:

```koskari
define greet [name greeting: "hello" punct: "!"]:
  (print (+ greeting " " name punct))

(greet "Ada")
(greet "Grace" greeting: "Hi" punct: ".")
```

Use defaults for friendly APIs: callers can stay short for the common case and
be explicit when they need a variation.

---

## By-ref parameters

Normally a parameter receives a *copy* of the argument's value, so reassigning
it inside the function has no effect on the caller. Mark a parameter **by-ref**
with a leading `&` to change that: the parameter shares storage with the
caller's binding, and a `set!` inside the function writes through to it.

```koskari
define add-a [a &result]:
  (set! result (+ a 1))

define value 100
(add-a value &value)
; value is now 101
```

There are two `&` here, doing different jobs:

- `&result` in the **parameter list** declares the by-ref parameter.
- `&value` at the **call** passes a *reference* to `value` (the `&name`
  shorthand) so the parameter has something to alias. You can pass any
  reference, e.g. `&(item xs 0)` to write back into a list slot.

By-ref works on ordinary positional and keyword parameters only — not on rest
(`*`) or keyword-rest (`**`) parameters. Use it sparingly: most code is clearer
returning values than mutating the caller's bindings.

---

## Anonymous functions

Use `lambda` when a small function value does not need a top-level name:

```koskari
(define add-one (lambda [x] (+ x 1)))
(add-one 41)
```

Named functions are usually clearer for tutorials and public code.

---

## Recursion

A recursive function calls itself. Always write the stopping case first:

```koskari
define countdown [n]:
  if (<= n 0):
    (print "done")
  else:
    (print n)
    (countdown (- n 1))
```

For loops over changing counters, `while` is often easier. Use recursion when
the problem naturally has “one item plus the rest.”

---

## What you should remember

Functions are values with inputs and a body. The last body form is the normal
result. Defaults make calls friendlier. Recursion needs a clear stopping case.
