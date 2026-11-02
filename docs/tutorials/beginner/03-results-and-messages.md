# Results and messages

Running a program produces output, and sometimes an error. Reading both
is part of programming.

## Results at the prompt

:::{dialogue}
When does Koskari show a result?
+++
At the prompt, whenever a line produces a value worth seeing:

```koskari
>>> (+ 1 2)
3
```
:::

:::{dialogue}
What about `(var x 99)`?
+++
Nothing is shown. Giving a name a value is a quiet job; there is no
result to look at. Ask for `x` afterwards and you see `99`.
:::

:::{dialogue}
And `(print "hi")`?
+++
You see `hi`, because that is what `print` does. Its own result is
nothing worth showing, so the prompt shows only the printed text.
:::

:::{dialogue}
Can we get the last result back?
+++
Yes. An underscore names the value from the last line:

```koskari
>>> (+ 3 4)
7
>>> (* _ 2)
14
```
:::

## Errors while reading

:::{dialogue}
What if the punctuation is wrong?
+++
Koskari cannot read the program and says so:

```koskari
>>> ( 1 2 ]
  File "<stdin>", line 1
    ( 1 2 ]
          ^
koskari.exceptions.ReaderSyntaxError: mismatched open and close characters
```

The last line is the name of the error and what went wrong. The lines
above show where: the line you typed, with a caret under the character
that stopped the reader. From a file the first line names the file
instead of `<stdin>`.
:::

:::{dialogue}
What should we do with it?
+++
Read the last line of the message. Look back at what you typed and
count the `(` and `)`. Fix one thing and run again.
:::

## Errors while running

:::{dialogue}
Can the punctuation be right and the program still fail?
+++
Yes. Koskari can read this, but cannot do it:

```koskari
>>> (/ 1 0)
Traceback (most recent call last):
  File "<stdin>", line 1, in __main__
ZeroDivisionError: division by zero
```

The last line names the error. Reading was fine; running was not, and
the lines above say where.
:::

:::{dialogue}
So there are two kinds of error?
+++
Two kinds. A syntax error means Koskari could not understand the text.
A runtime error means it understood the text and something went wrong
doing it. The last line of the message tells you which: a reader error
ends in `ReaderSyntaxError`, a runtime error in the name of what failed.
:::

## Lines that continue

:::{dialogue}
What if I press Enter before the group is closed?
+++
Koskari waits for the rest. The prompt changes until the `)` arrives:

```koskari
>>> (+ 1
...    2)
3
```
:::
