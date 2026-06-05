# From source to stack

This optional walkthrough connects one source form to the evaluator picture from
[Bytecode mental model](bytecode-mental-model.md).

---

## Example source

Start with a familiar form:

```koskari
(print "total:" (+ 20 22))
```

It has one outer call, `print`, and one inner call, `+`.

## Step 1: read

The reader turns text into a tree-shaped form:

```text
call print
├── "total:"
└── call +
    ├── 20
    └── 22
```

This is why punctuation matters. If the parentheses do not match, Koskari never
gets this tree.

## Step 2: compile

The compiler turns the tree into evaluator instructions. In plain language:

1. prepare to call `print`,
2. prepare the string argument,
3. compile the nested `(+ 20 22)` call,
4. call `print` with the two resulting arguments.

The compiler may choose specialized instructions for common shapes, but the
meaning stays the same.

## Step 3: evaluate

At runtime, the inner call produces `42`. Then the outer call receives
`"total:"` and `42`.

```text
(+ 20 22)       -> 42
(print "total:" 42)
```

The print effect is visible on screen. The exact return value of `print` is less
important than the effect for this example.

## Practice trace

Try drawing the same tree for:

```koskari
(if (gt score 79)
  (print "pass")
  (print "try again"))
```

Ask three questions:

1. What is the outer form?
2. Which forms are inputs or branches?
3. Which nested form must produce a value before a choice can be made?

## What you should remember

Source is text, reading makes structure, compiling makes evaluator steps, and
evaluation produces values and effects.
