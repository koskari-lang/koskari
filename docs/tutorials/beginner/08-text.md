# Text

Text is a value like a number is, and it has its own tools.

## Joining and measuring

:::{dialogue}
Can `+` add text?
+++
Yes. It joins the pieces end to end:

```koskari
koskari> (+ "Ada" " " "Lovelace")
"Ada Lovelace"
```
:::

:::{dialogue}
How long is a piece of text?
+++
`len` counts its characters, the same `len` that counts a list:

```koskari
koskari> (len "Hello")
5
```
:::

## Putting values into text

:::{dialogue}
How do we put a name's value into a message?
+++
With an `f` before the opening quote, and the name in braces:

```koskari
define name "Ada"
define age 36

print f"{name} is {age}"
```

That prints `Ada is 36`.
:::

:::{dialogue}
Can a call go in the braces?
+++
Yes, any call:

```koskari
koskari> f"{name} has {(len name)} letters"
"Ada has 3 letters"
```
:::

## Methods on text

:::{dialogue}
How do we make text loud?
+++
Text has methods, called with a dot after the value:

```koskari
koskari> ("hello".upper)
"HELLO"
koskari> (name.lower)
"ada"
```

The parentheses make it a call. Without them, `name.lower` is the method
itself, not its result.
:::

:::{dialogue}
How do we split a sentence into words?
+++
`split` gives a list of the words:

```koskari
koskari> ("the quick brown fox".split)
#["the" "quick" "brown" "fox"]
```

And `join` goes the other way. It is called on the text to put between
the items:

```koskari
koskari> (" ".join #["one" "two" "three"])
"one two three"
```
:::

## Numbers and text

:::{dialogue}
Is `"42"` the same as `42`?
+++
No. One is text, the other is a number. `(+ "42" 1)` is an error. `int`
turns text into a number and `str` turns a number into text:

```koskari
koskari> (+ (int "42") 1)
43
koskari> (+ (str 42) "!")
"42!"
```
:::

:::{dialogue}
What does this print?
+++
```koskari
define words #["koskari" "is" "small"]

for-each [word words]:
  print f"{(word.upper)} has {(len word)} letters"

print (" ".join words)
```

Three lines like `KOSKARI has 7 letters`, then `koskari is small`.
:::
