# From source to stack

This optional trace connects the beginner mental model to the evaluator mental
model.

---

## Source

Start with source text:

```koskari
(print "total:" (+ 2 3))
```

---

## Read

The reader recognizes one outer call whose first position is `print`. The second
input is another call, `(+ 2 3)`.

---

## Compile

The compiler emits instructions that evaluate the callable and arguments in an
order the evaluator can execute. Constants like `"total:"`, `2`, and `3` can be
stored directly. Names like `print` and `+` are looked up when needed.

---

## Evaluate

At runtime, the inner call leaves `5` as a value. The outer call then receives
`"total:"` and `5`, prints them, and leaves its own result.

---

## Debugging with the trace

When a program surprises you, ask which stage failed:

- **Read:** punctuation or layout problem.
- **Compile:** unknown special shape or unsupported form.
- **Evaluate:** the program ran into a bad value, missing name, failed import,
  or raised exception.

---

## What you should remember

Source becomes forms, forms become instructions, and instructions manipulate
values. Most user bugs can be located by asking whether the problem is reading,
compiling, or evaluating.
