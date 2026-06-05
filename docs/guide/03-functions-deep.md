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
