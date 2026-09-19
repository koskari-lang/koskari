# Calling things

Most of what a program does is call something with inputs.

## The shape of a call

:::{dialogue}
What is a call?
+++
A group whose first word is the thing to do. The rest are its inputs:

```koskari
(print "Hello")
```

`print` is the thing to do. `"Hello"` is the input.
:::

:::{dialogue}
What is `(+ 1 2)`?
+++
A call. `+` is the thing to do; `1` and `2` are the inputs.
:::

:::{dialogue}
What does `(+ 1 2)` produce?
+++
`3`. Decide first, then check:

```koskari
>>> (+ 1 2)
3
```
:::

:::{dialogue}
And `(+ 10 5)`?
+++
`15`.
:::

:::{dialogue}
And `(* 6 7)`?
+++
`42`. The `*` is how multiplication is spelled.
:::

:::{admonition} The call rule
:class: ko-rule

The first thing inside the parentheses is what to do. Everything after
it is an input.
:::

## More than one input

:::{dialogue}
Can a call have many inputs?
+++
Yes:

```koskari
>>> (+ 1 2 3 4)
10
```

`+` adds every input it is given.
:::

:::{dialogue}
What does `(print "a" "b" "c")` do?
+++
Calls `print` with three inputs. It puts each on the screen, in order,
with a space between:

```koskari
>>> (print "a" "b" "c")
a b c
```
:::

## Giving a name a value

:::{dialogue}
Can we keep a value under a name?
+++
Yes, with `define`:

```koskari
>>> (define x 6)
>>> (define y 7)
```

From then on `x` means `6` and `y` means `7`.
:::

:::{dialogue}
What does `(print x y (* x y))` show?
+++
Three inputs: the value of `x`, the value of `y`, and the value of
`(* x y)`. Work out the third before you run it.

```koskari
>>> (print x y (* x y))
6 7 42
```
:::

## Calls inside calls

:::{dialogue}
Can an input be another call?
+++
Yes. That is what `(* x y)` was, sitting inside the `print` call.
Koskari works from the inside out: the inner call finishes first, and its
value becomes the input to the outer one.
:::

:::{dialogue}
What is `(+ 1 (+ 2 3))`?
+++
The inner call is `5`, then `+` adds `1` and `5`:

```koskari
>>> (+ 1 (+ 2 3))
6
```
:::
