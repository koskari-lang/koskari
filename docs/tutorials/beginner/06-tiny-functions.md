# Tiny functions

A function is a recipe with a name. Give it inputs, it runs its body, and
it hands back a result.

## Naming a recipe

:::{dialogue}
How do we make our own thing to call?
+++
With `define`, a name, the inputs in square brackets, and a block:

```koskari
define double [n]:
  (* n 2)
```

`double` is the name. `[n]` says the function takes one input, called
`n` inside the body.
:::

:::{dialogue}
How do we use it?
+++
Call it like anything else:

```koskari
koskari> (double 8)
16
```
:::

## The result

:::{dialogue}
Where does the `16` come from?
+++
From the last line of the body. `(* n 2)` with `n` as `8` is `16`, and
that is what the call hands back.
:::

:::{dialogue}
What if the body has several lines?
+++
They run in order, and the last one is the result:

```koskari
define add-tax [price]:
  define tax (* price 0.08)
  (+ price tax)
```

The `define tax` line sets up a helper name. The `(+ price tax)` line is
the result.
:::

:::{admonition} The last-line rule
:class: ko-rule

A function's result is the value of the last line in its body.
:::

## More than one input

:::{dialogue}
Can a function take several inputs?
+++
Put each name in the brackets:

```koskari
define rectangle-area [width height]:
  (* width height)

print "area:" (rectangle-area 6 7)
```
:::

:::{dialogue}
Do the names matter?
+++
To Koskari, no. To the next person reading, `width` and `height` say
more than `a` and `b`.
:::

## A function that chooses

:::{dialogue}
Can a function use `if`?
+++
A body can hold anything you have used so far:

```koskari
define describe-score [score]:
  if (>= score 10):
    "great"
  elif (>= score 5):
    "good"
  else:
    "keep practicing"

print (describe-score 7)
```

The `if` is the last line, so the value of the branch that ran is the
result: `good`.
:::

:::{dialogue}
What does this print?
+++
```koskari
define badge [name points]:
  if (>= points 100):
    print name "earned a gold badge"
  elif (>= points 50):
    print name "earned a silver badge"
  else:
    print name "is still warming up"

(badge "Ada" 120)
(badge "Grace" 40)
```

`Ada earned a gold badge`, then `Grace is still warming up`. Change the
two numbers and decide again.
:::
