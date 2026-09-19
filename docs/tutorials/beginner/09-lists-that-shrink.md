# Lists that shrink

A function can call itself. That is how a Lisp walks a list, and it is
the idea this language family is built on.

## Another kind of list

:::{dialogue}
What is `'(apple pear plum)`?
+++
A list of three names. The single quote in front says: keep this group
as it is, do not call `apple`.

```koskari
>>> '(apple pear plum)
(apple pear plum)
```

This is the older kind of list, the one Lisp is named for. Chapter 7's
`#[...]` is the everyday kind; this kind is the one that shrinks.
:::

:::{dialogue}
How do we get the first item?
+++
`car`:

```koskari
>>> (car '(apple pear plum))
apple
```
:::

:::{dialogue}
And everything after the first?
+++
`cdr`, which is the rest of the list, itself a list:

```koskari
>>> (cdr '(apple pear plum))
(pear plum)
```

The names are old and odd. Read `car` as "the first" and `cdr` as "the
rest".
:::

:::{dialogue}
What is the rest of `'(plum)`?
+++
The empty list, written `nil` or `'()`. `nil?` asks whether a list is
empty:

```koskari
>>> (cdr '(plum))
()
>>> (nil? (cdr '(plum)))
True
```
:::

:::{dialogue}
How do we put an item on the front?
+++
`cons`:

```koskari
>>> (cons 'fig '(apple pear))
(fig apple pear)
```
:::

## Counting by shrinking

:::{dialogue}
How long is `'(apple pear plum)`?
+++
One more than the length of its rest, `'(pear plum)`.
:::

:::{dialogue}
How long is `'(pear plum)`?
+++
One more than the length of `'(plum)`.
:::

:::{dialogue}
How long is `'(plum)`?
+++
One more than the length of `'()`.
:::

:::{dialogue}
And how long is `'()`?
+++
Zero. This is the only question that has a plain answer, so it is the
one to ask first.
:::

:::{dialogue}
Can we write that as a function?
+++
Ask the plain question first, and otherwise ask the same question of the
rest:

```koskari
define length [items]:
  if (nil? items):
    0
  else:
    (+ 1 (length (cdr items)))

print (length '(apple pear plum))
```

That prints `3`. `length` calls itself on a shorter list each time,
until the list is empty.
:::

:::{admonition} The shrinking rule
:class: ko-rule

Ask `nil?` first. Otherwise, do something with the `car` and call
yourself on the `cdr`.
:::

## Looking for something

:::{dialogue}
Is `'pear` in `'(apple pear plum)`?
+++
Ask `nil?` first: no, the list is not empty. Is `'pear` the `car`? No,
that is `'apple`. So ask the same of the rest.
:::

:::{dialogue}
Is `'pear` in `'(pear plum)`?
+++
Not empty; the `car` is `'pear`. Yes.
:::

:::{dialogue}
Write it.
+++
```koskari
define member? [item items]:
  if (nil? items):
    False
  elif (== item (car items)):
    True
  else:
    (member? item (cdr items))

print (member? 'pear '(apple pear plum))
print (member? 'kiwi '(apple pear plum))
```

`True`, then `False`. Names that ask a question end in `?` by custom.
:::

## Building a new list

:::{dialogue}
How do we take `'pear` out of `'(apple pear plum)`?
+++
Ask `nil?` first: an empty list with nothing removed is still empty. If
the `car` is the item, the answer is the rest. Otherwise keep the `car`
and remove the item from the rest:

```koskari
define remove [item items]:
  if (nil? items):
    nil
  elif (== item (car items)):
    (cdr items)
  else:
    (cons (car items) (remove item (cdr items)))

print (remove 'pear '(apple pear plum))
```

`(apple plum)`. The `cons` puts each kept item back on the front of the
answer for the rest.
:::

:::{dialogue}
Why keep two kinds of list?
+++
`#[...]` lists are Python's, and every Python library speaks them. The
quoted kind is what Koskari itself is written in: a program is a list of
lists, and the functions in this chapter are how the language reads its
own source. Use `#[...]` for data and this kind when a problem is one
item and the rest.
:::
