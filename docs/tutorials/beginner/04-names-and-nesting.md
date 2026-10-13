# Names and nesting

The same two rules keep coming back: a group is a call, and inner calls
finish first.

## Every group is the same shape

:::{dialogue}
What is the shape of `(+ 1 2)`?
+++
```text
( thing  input ... )
```

`+` is the thing to do; `1` and `2` are inputs.
:::

:::{dialogue}
Does `(print "a" "b")` have the same shape?
+++
Yes. `print` is the thing; `"a"` and `"b"` are its inputs.
:::

:::{dialogue}
Does `(var total 0)` have the same shape?
+++
Nearly. `var` is the thing to do; `total` and `0` are what it works
with. The difference is what it does: it gives a name a value instead of
producing one.
:::

## Inside out

:::{dialogue}
What is `(+ (+ 1 2) 3)`?
+++
Inner first: `(+ 1 2)` is `3`. Then `(+ 3 3)` is `6`.
:::

:::{dialogue}
What is `(print (+ 2 3))`?
+++
The inner call produces `5`, and `print` puts `5` on the screen.
:::

:::{dialogue}
What is `(+ 1 (+ 2 (+ 3 4)))`?
+++
From the inside: `7`, then `9`, then `10`. However deep the nesting, the
rule is the same.
:::

:::{admonition} The inside-out rule
:class: ko-rule

An inner call finishes before the call around it uses its value.
:::

## Names inside calls

:::{dialogue}
If `width` is `10` and `height` is `4`, what is `(* width height)`?
+++
`40`:

```koskari
>>> (var width 10)
>>> (var height 4)
>>> (* width height)
40
```
:::

:::{dialogue}
What does `(print "area:" (* width height))` show?
+++
Two inputs reach `print`: the text and the number the inner call made.

```koskari
>>> (print "area:" (* width height))
area: 40
```
:::

## Several lines in a file

:::{dialogue}
Can a file hold more than one call?
+++
Yes. Koskari runs them from top to bottom. Save this as `numbers.ki`:

```koskari
(var x 6)
(var y 7)
(print x y (* x y))
```

Run `koskari numbers.ki` and you see `6 7 42` on one line, from the
single `print`.
:::

:::{dialogue}
Is that all a program is?
+++
So far, yes: calls, run in order, each built from smaller calls. The
next chapter adds a way to write them without so many parentheses.
:::
