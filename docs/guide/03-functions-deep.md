# Functions deep

Functions are values you can call. They let you name a calculation, reuse it,
and pass it around.

---

## Defining a function

The common shorthand is `define` plus a parameter list:

```koskari
(define double [x]
  (* x 2))

(double 21)
```

The body runs when the function is called. The value of the body’s last form is
the function result.

## Anonymous functions

Use `lambda` when you need a function value without immediately giving it a
top-level name:

```koskari
((lambda [x] (* x x)) 6)
```

This creates a function and calls it right away. In larger programs, anonymous
functions are useful as callbacks and small adapters.

## Multiple parameters and defaults

Parameter lists are written in brackets:

```koskari
(define rectangle-area [width height]
  (* width height))
```

Koskari formals also support keyword-style parameters with defaults:

```koskari
(define greet [name punctuation: "!"]
  (+ "Hello, " name punctuation))
```

Callers can accept the default or provide a different value.

## Documentation strings

If the first body form is a string, Koskari records it as the function’s
docstring:

```koskari
(define square [x]
  "Return x multiplied by itself."
  (* x x))
```

Docstrings make REPL help and generated API pages friendlier.

## Recursion

A function may call itself by name after it has been defined:

```koskari
(define factorial [n]
  (cond
    [(eq n 0) 1]
    [else: (* n (factorial (- n 1)))]))
```

Recursion needs a stopping case. Here `(eq n 0)` stops the chain.

## Return annotations

Function forms can record return annotations using `::` or `type:`:

```koskari
(define answer :: int []
  42)
```

Current annotations are metadata for tools and interop; write functions so the
body still computes the correct value.

## What you should remember

- Functions are callable values.
- `define` function shorthand is the everyday way to create named functions.
- `lambda` creates a function expression.
- The last body form is the usual result.
- Docstrings and annotations help readers and tools.

Next: [Data structures](04-data-structures.md).
