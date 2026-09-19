# Words and punctuation

A program is made of words and punctuation. This chapter is about telling
them apart.

## Numbers

:::{dialogue}
Is `42` a program?
+++
It is a number, one kind of value. Type it at the prompt and Koskari
shows it back:

```koskari
koskari> 42
42
```
:::

:::{dialogue}
Are `0` and `-3` numbers too?
+++
Yes. So is `2.5`.
:::

## Text

:::{dialogue}
How do we write text in a program?
+++
Between double quotes:

```koskari
koskari> "Hello, Koskari!"
"Hello, Koskari!"
```

The quotes are punctuation. They mark where the text starts and stops
and are not part of it. Koskari shows a value back with its quotes so
you can see it is text; `print` shows the text alone.
:::

:::{dialogue}
Can we use single quotes instead?
+++
No. A single quote means something else, which chapter 9 uses. Text is
always in `"` quotes.
:::

## Names

:::{dialogue}
What is `x`?
+++
By itself, a name. A name is a word you choose. It means nothing until
you give it a value, which chapter 2 does.
:::

:::{dialogue}
Is `print` a name?
+++
Yes, and one that already has a value: the thing that puts text on the
screen.
:::

:::{dialogue}
Can a name contain digits, or a dash?
+++
Both: `x2`, `add-tax`, and `total-so-far` are names. A name can even
start with a digit, as long as the whole word is not a number: `2x` is a
name and `2` is a number.
:::

## Parentheses

:::{dialogue}
What does `(` do?
+++
It starts a group, and the matching `)` ends it. Everything between them
belongs together:

```koskari
(+ 1 2)
```

The group is a call, which is chapter 2. The parentheses are not
decoration; they tell Koskari where the group begins and ends.
:::

:::{dialogue}
Can a group sit inside another group?
+++
Yes, to any depth:

```koskari
(+ 1 (+ 2 3))
```
:::

## Spacing

:::{dialogue}
Is `(+ 1 2)` different from `(+  1   2)`?
+++
No. Inside a group, one space or several are the same. Use spaces so
that a person can see the pieces.
:::

:::{dialogue}
What about `(+1 2)`?
+++
That is different. A `+` or `-` touching what follows it belongs to it:
`+1` is the number `1` with a sign, not the name `+` beside a `1`. So
`(+1 2)` asks the number `1` to do something with `2`, and Koskari
reports an error. Put a space after the name you are calling.
:::

:::{admonition} The group rule
:class: ko-rule

Every `(` has a matching `)`. Leave one out and Koskari cannot read the
program at all.
:::
