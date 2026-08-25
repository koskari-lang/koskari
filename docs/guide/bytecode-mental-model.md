# Bytecode mental model

You can use Koskari without knowing bytecode.

---

## From form to instructions

After Koskari reads source text into forms, the compiler turns those forms into
instructions for the evaluator. A call such as:

```koskari
(+ 1 2)
```

roughly means:

1. Put the callable `+` where the evaluator can use it.
2. Put `1` and `2` on the operand stack.
3. Call with two inputs.
4. Leave the result where the surrounding form can use it.

The exact instruction names belong in the VM reference; the mental
model is “evaluate pieces, keep intermediate values on a stack, then combine.”

---

## Why this helps

The stack model explains why nested calls run inside-out:

```koskari
(print (+ 1 2))
```

The inner `(+ 1 2)` must leave `3` behind before `print` can receive it.

---

## When to read the VM reference

Read the VM reference when you are debugging compiler output, implementing a
special form, or trying to understand control-flow behavior precisely. Skip it
when you are learning everyday Koskari syntax.

---

## What you should remember

Bytecode is the evaluator's instruction language. For most users, the useful
picture is a stack of intermediate values plus jumps for control flow.
