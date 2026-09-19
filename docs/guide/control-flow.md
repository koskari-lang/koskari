# Control flow

Control flow decides which forms run and how many times they run.

---

## Branching with `if`

Use layout `if` for reader-friendly branches:

```koskari
if (> score 90):
  (print "excellent")
elif (> score 70):
  (print "solid")
else:
  (print "keep practicing")
```

The lower-level reference form is `cond`: clauses are tested from top to bottom,
and the first true clause wins.

```koskari
(cond
  [(> score 90) "excellent"]
  [(> score 70) "solid"]
  [else: "keep practicing"])
```

---

## Repeating with `while`

Use `while` when a condition controls the loop:

```koskari
(define n 3)
while (> n 0):
  (print n)
  (set! n (- n 1))
else:
  (print "liftoff")
```

The optional `else:` body runs when the loop finishes normally. A `break` skips
that `else:` body.

---

## Repeating over values

Use `for-each` when you already have a collection:

```koskari
for-each [name names]:
  (print "hello" name)
else:
  (print "all greeted")
```

Inside loops, `continue` jumps to the next iteration and `break` leaves the
loop. `break` can also carry a value in expression-oriented code.

---

## Boolean combinations

Use `and` and `or` for checks made from smaller checks:

```koskari
if (and (>= age 13) (< age 20)):
  (print "teen ticket")
```

Keep conditions short. If a condition gets hard to read, define a helper name or
function.

---

## What you should remember

Use `if` / `elif` / `else` for choices, `while` for condition-driven repetition,
and `for-each` for collections. Put the clearest stopping rule where readers can
see it.

---

**Go deeper:** [Demand, async, and defer](demand-async-and-defer.md)
— iteration and control flow on Koskari's async-native evaluator.
