# A program

Everything so far, in one file.

## The shape of a file

:::{dialogue}
What goes in a file, and in what order?
+++
Definitions first, then the lines that use them. Koskari runs top to
bottom, so a function has to be defined before the line that calls it.
:::

:::{dialogue}
What is `when-main:`?
+++
A block that runs only when the file is run directly with `koskari`, not
when another file borrows its definitions. Put the "do it now" lines
there:

```koskari
define greet [name]:
  print f"hello, {name}"

when-main:
  (greet "Ada")
```
:::

## Borrowing from Python

:::{dialogue}
Does Koskari have a square root?
+++
Python does, and Koskari can use anything Python has. `import` brings a
module in, and a dot reaches into it:

```koskari
import math

print (math.sqrt 16)
```

That prints `4.0`.
:::

:::{dialogue}
Can we bring in just one thing?
+++
`import-from` names the module and then the things you want:

```koskari
import-from math sqrt pi

print (sqrt 16) pi
```
:::

## The program

:::{dialogue}
Put it together.
+++
Save this as `report.ki` and run `koskari report.ki`:

```koskari
import-from statistics mean

define scores #[72 88 95 61 88]

define average [numbers]:
  (round (mean numbers) 1)

define grade [score]:
  if (>= score 90):
    "A"
  elif (>= score 80):
    "B"
  elif (>= score 70):
    "C"
  else:
    "D"

define report [numbers]:
  for-each [score numbers]:
    print f"{score}: {(grade score)}"
  print f"average {(average numbers)}, best {(max numbers)}"

when-main:
  (report scores)
```
:::

:::{dialogue}
What does it print?
+++
```text
72: C
88: B
95: A
61: D
88: B
average 80.8, best 95
```

Five lines from the loop, then one from the last `print`.
:::

:::{dialogue}
What in it is new?
+++
Only `statistics.mean`, borrowed from Python, and `round`, which trims a
number to one decimal place. Everything else is a list, a function, a
choice, a loop, and text, from the chapters before.
:::

:::{dialogue}
What would you change first?
+++
The scores. Then the grade boundaries. Then add a line to `report` that
counts how many scores are above the average, which needs a `define`
before the loop and an `if` inside it.
:::
