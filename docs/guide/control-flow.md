# Control flow

Which forms run, and how many times. Every control form is an
expression with a value.

## Branching

Layout `if` gathers `elif` and `else` peers and reads as one `cond`:

```koskari
if (> score 90):
  print "excellent"
elif (> score 70):
  print "solid"
else:
  print "keep practicing"
```

`cond` is the form underneath. Clauses are tested from the top and the
first true one wins; with no match and no `else:`, the value is `None`:

```koskari
(cond
  [(> score 90) "excellent"]
  [(> score 70) "solid"]
  [else: "keep practicing"])
```

Because an `if` has a value, one `var` can take a whole choice:

```koskari
var label:
  if (> score 90):
    "excellent"
  else:
    "ordinary"
```

`when` and `unless` are the one-armed forms. `when` runs its body if the
test is true, `unless` if it is false, and both yield `None` otherwise:

```koskari
when (> n 0):
  print n

unless ready:
  return "not yet"
```

Tests use Python truthiness: `0`, `""`, `#[]`, and `None` are false.

## Choosing on a value

`switch` compares one value against candidates with `==`. `match`
destructures it against patterns: sequences, mappings, classes, typed
captures, with guards. The pattern language has its own page,
[Match patterns](match-patterns.md).

```koskari
match msg:
  case #[(tag :: keyword) *: args]:
    (handle tag args)
  case #{"kind": "job" "url": u}:
    (fetch u)
  else:
    (ignore msg)
```

## Boolean operators

`and` and `or` short-circuit and return the deciding operand, as in
Python; `not` returns a boolean:

```koskari
if (and (>= age 13) (< age 20)):
  print "teen ticket"
```

Comparisons chain: `(< 0 x 10)` is `0 < x < 10`.

## `while`

```koskari
var n 3
while (> n 0):
  print n
  decr! n
else:
  print "liftoff"
```

The optional `else:` body runs when the test becomes false. A `break`
skips it.

## `for-each`

```koskari
for-each [name names]:
  print "hello" name
else:
  print "all greeted"
```

`for-each` walks any Python iterable. The binding can destructure:
`for-each [[k v] (d.items)]:`. The `else:` peer runs when the iterable
is exhausted without a `break`.

## `break` and `continue`

`continue` jumps to the next iteration. `break` leaves the loop, and may
carry a value, which becomes the value of the loop:

```koskari
var found:
  for-each [v values]:
    when (> v 2):
      break v
  else:
    None
```

Both are calls, `(break)` and `(continue)`, and both have a bare form on
a layout line. Inside a loop's `else:` clause the loop has already
finished, so a `break` or `continue` written there belongs to the
enclosing loop, as in Python; with no enclosing loop it is an error.

## Comprehensions

`list-each`, `dict-each`, and `each` produce a list, a dict, and a
generator from the same `[var seq]` binding, with a `when:` or
`unless:` clause on the header as the filter:

```koskari
list-each [x (range 10)] when: (== 0 (% x 2)):
  (* x x)
```

They are for values; `for-each` is for effects.

## Early exit from a function

`return` inside any of these forms leaves the function. It is a call,
so `(return x)`, and a bare `return` on its own line is `(return)`.
[Functions](functions.md) covers the multiple-value forms.

## Exceptions

`try` is control flow too, and an expression. It is on
[I/O and effects](io-and-effects.md).

## What runs when

These forms all run on the same stackless evaluator, so a loop body that
awaits suspends the whole function and resumes in place. Nothing about
`while` or `for-each` changes under async code; the detail is in
[Demand, async, and defer](demand-async-and-defer.md).
