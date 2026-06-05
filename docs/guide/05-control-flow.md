# Control flow

Control flow decides which forms run, how often they run, and when a sequence
stops.

---

## Two-way choice with `if`

Use `if` for one question and two outcomes:

```koskari
(if (gt temperature 80)
  "hot"
  "comfortable")
```

The condition runs first. If it is truthy, Koskari evaluates the then branch;
otherwise it evaluates the else branch. When the else branch is omitted, the
fallback value is `None`.

## Multi-way choice with `cond`

Use `cond` when several tests should be tried in order:

```koskari
(cond
  [(gt score 89) "A"]
  [(gt score 79) "B"]
  [(gt score 69) "C"]
  [else: "keep practicing"])
```

The first true test wins. `else:` is the catch-all clause.

## Short conditional helpers

`when` runs a body only when a condition is truthy:

```koskari
(when debug
  (print "debug mode"))
```

`unless` is the opposite:

```koskari
(unless ready
  (print "not ready yet"))
```

Use these for side effects. Use `if` or `cond` when you want a meaningful value
from both branches.

## Loops

Use `while` when a condition controls repetition:

```koskari
(define n 3)
(while (gt n 0)
  (print n)
  (setq n (- n 1)))
```

Use `for-each` for each item in a sequence:

```koskari
(for-each [name (list "Ada" "Grace")]
  (print name))
```

Both loop forms support `break` and `continue`. `break` exits the nearest loop;
`continue` starts the next iteration.

## Loop `else`

A loop may have an `else:` clause that runs only when the loop finishes normally,
not when `break` exits it:

```koskari
(for-each [name names]
  (when (eq name target)
    (break "found"))
  [else: "not found"])
```

This is handy for search code.

## What you should remember

- `if` chooses between two branches.
- `cond` chooses the first matching clause.
- `when` and `unless` are readable for one-sided effects.
- `while` repeats while a condition stays true.
- `for-each` repeats over a sequence.
- `break`, `continue`, and loop `else:` give you precise loop exits.

Next: [Modules and program structure](06-modules-and-program-structure.md).
