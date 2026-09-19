# Lists

One name can hold many values at once.

## Making a list

:::{dialogue}
How do we keep three fruits under one name?
+++
In a list, written with `#[` and `]`:

```koskari
define fruits #["apple" "pear" "plum"]
```

The items are separated by spaces, nothing else.
:::

:::{dialogue}
Why the `#`?
+++
Because plain `[apple pear plum]` would be a group, and a group is a
call. The `#` says: this is a list of values, not something to do.
:::

:::{dialogue}
How many items does it have?
+++
`len` counts:

```koskari
koskari> (len fruits)
3
```
:::

## Picking an item

:::{dialogue}
How do we get the first fruit?
+++
With its position in square brackets, touching the name. Positions start
at `0`:

```koskari
koskari> fruits[0]
'apple'
koskari> fruits[2]
'plum'
```
:::

:::{dialogue}
And the last one, without counting?
+++
`-1` counts from the end:

```koskari
koskari> fruits[-1]
'plum'
```
:::

## Changing a list

:::{dialogue}
Can we add an item?
+++
Every list has an `append` method. A method is called with a dot after
the name, inside parentheses:

```koskari
koskari> (fruits.append "fig")
koskari> fruits
['apple', 'pear', 'plum', 'fig']
```

`append` changes the list in place and shows nothing.
:::

:::{dialogue}
Is something in the list?
+++
`in` asks:

```koskari
koskari> (in "pear" fruits)
True
koskari> (in "kiwi" fruits)
False
```
:::

## Doing something with each item

:::{dialogue}
How do we print every fruit?
+++
`for-each` runs its body once per item, with the item under a name:

```koskari
for-each [fruit fruits]:
  print "I like" fruit
```

The brackets hold the name to use and the list to walk.
:::

:::{dialogue}
Can we add up a list of numbers?
+++
`sum` does that in one call:

```koskari
koskari> (sum #[3 4 5])
12
```
:::

:::{dialogue}
Can we make a new list out of an old one?
+++
`list-each` builds a list from the value of its body, once per item:

```koskari
define prices #[3 4 5]

define doubled:
  list-each [price prices]:
    (* price 2)

print doubled
```

That prints `[6, 8, 10]`. `prices` is unchanged.
:::

:::{dialogue}
What does this print?
+++
```koskari
define scores #[7 10 4]

for-each [score scores]:
  if (>= score 10):
    print score "is a top score"
  else:
    print score "is a score"

print "best:" (max scores)
```

Three lines, one per score, then `best: 10`.
:::
