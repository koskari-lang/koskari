# Bytecode mental model

You do not need bytecode to write Koskari programs. This optional chapter gives
a simple picture for readers who want to understand what happens below the
source level.

---

## Source becomes instructions

Koskari source is first read into forms, then compiled into smaller evaluator
instructions. A source expression like:

```koskari
(+ 1 2)
```

can be thought of as:

1. put the callable `+` where the evaluator can reach it,
2. put the values `1` and `2` where the call can use them,
3. call the callable,
4. leave the result for the surrounding expression.

The exact opcode names are reference material; the important idea is that a
large form becomes a sequence of small steps.

## A stack picture

Many instructions use a stack: push values on, pop values off, and leave a
result behind.

```text
push +
push 1
push 2
call with 2 inputs
=> 3
```

Nested calls use the same idea. The inner call leaves its result, and the outer
call consumes that result as one of its inputs.

## Control flow is jumps

Conditionals and loops compile to tests and jumps:

```koskari
(if ready "go" "wait")
```

The evaluator checks `ready`. If it is false, it jumps over the true branch to
the false branch. Loops are similar, except one jump goes back to the next
iteration.

## Why this helps

A bytecode mental model can make error reports and disassembly easier to read:

- values must be available before a call can consume them,
- branches choose which instruction sequence runs,
- loops repeat by jumping back,
- `break` and `continue` are controlled exits from loop regions.

## Where to go next

Use the [VM reference](../reference/vm/index.md) when you need exact opcode
names and stack effects. Treat it as a lookup table, not a prerequisite for
learning the language.
