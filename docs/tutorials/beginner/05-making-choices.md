# Making choices

A program that can take one path or another is more useful than one
that cannot. This chapter also introduces a second way of writing
Koskari, which most files use.

## Lines

:::{dialogue}
Does every call need parentheses?
+++
In a file, no. A line with more than one thing on it is a call, with the
first thing as the thing to do:

```koskari
define temperature 72
print "it is" temperature "degrees"
```

That is the same as `(define temperature 72)` and
`(print "it is" temperature "degrees")`.
:::

:::{dialogue}
What is a line with only one thing on it?
+++
That thing, by itself. `temperature` alone on a line is the value of
`temperature`, not a call.
:::

:::{admonition} The line rule
:class: ko-rule

A line with more than one thing on it is a call. A line with one thing
on it is that thing.
:::

## Asking a question

:::{dialogue}
How does a program choose?
+++
With `if`, a question, and an indented body:

```koskari
define temperature 72

if (> temperature 70):
  print "warm enough for a walk"
```

Read it as: if `temperature` is greater than `70`, print the message.
:::

:::{dialogue}
What is the `:` at the end of the `if` line for?
+++
It opens a block. The indented lines under it belong to the `if`, and
they run only when the question is true.
:::

:::{dialogue}
What does `>` do?
+++
Compares two numbers. `(> 3 10)` is `False`, `(< 3 10)` is `True`, and
`(== 3 3)` is `True`. A comparison is a call like any other: the thing
to do comes first.
:::

:::{admonition} The block rule
:class: ko-rule

A line ending in `:` opens a block. The lines indented under it belong
to that line.
:::

## Two paths

:::{dialogue}
Can we do something else when the question is false?
+++
Add `else:` at the same indent as the `if`:

```koskari
define score 8

if (>= score 10):
  print "bonus level unlocked"
else:
  print "keep going"
```

One of the two bodies runs, never both.
:::

## More than two paths

:::{dialogue}
And when there are several cases?
+++
`elif` asks another question when the one above it was false:

```koskari
define coins 17

if (>= coins 20):
  print "buy the big snack"
elif (>= coins 10):
  print "buy the small snack"
else:
  print "save your coins"
```

Koskari tries the questions from the top and runs the first body whose
question is true.
:::

## A choice with a value

:::{dialogue}
Can a choice produce a value instead of printing?
+++
Yes. The value of an `if` is the value of the body that ran:

```koskari
define age 12

define price:
  if (< age 13):
    5
  else:
    9

print "ticket price:" price
```

`define price:` opens a block too, and the `if` inside it is the value
that `price` gets.
:::

:::{dialogue}
What does this print?
+++
```koskari
define steps 4500

if (>= steps 10000):
  print "goal met"
elif (>= steps 5000):
  print "halfway there"
else:
  print "short walk today"
```

`short walk today`. Change the number and decide again before running.
:::
