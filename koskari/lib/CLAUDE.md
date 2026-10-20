# C Style

House style for the native extension. It applies to every `.c` and `.h` file
in the project, including `tests/native/`.

## File structure

- Begin every `.c` and `.h` file with the GPLv3 block comment, the standard
  multi-line `/* ... */` form, indented two spaces inside.
- Follow the license block with a `/** ... */` doc comment naming the file,
  describing its purpose, and carrying `:author:` and `:license:` fields.
  Do not add `:ai-assistant:` or similar per-file attribution.
- End every standalone `.c` file with `// The end.` on the last line.

## Indentation and width

- 2-space indentation throughout.
- Keep lines within 79 characters where practical.

## Function signatures

Parameters that fit on one line stay there:

```c
static void clear_and_free_transfer_plan(KosContext *ctx) {
```

When they would push past the limit, break to one parameter per line with a
4-space indent, closing `)` on the last parameter line immediately before
`{`:

```c
static int run_transfer_plan_to_completion(
    KosContext *ctx,
    KosEvent *out_ev) {
```

Never use alignment-based continuation. Do not align parameters to the
opening parenthesis. The same 4-space rule applies to call sites that wrap:

```c
PyObject *frame_ns = KosFunction_PrepareCallNamespaceFromVector(
    callable, args, nargs, kwnames);
```

## Blank line after the opening brace

A function using the expanded parameter style leaves a blank line between
`){` and the first statement:

```c
static KosEvalMode revive_plan(
    KosContext *ctx,
    PyObject *send_val,
    PyObject *throw_exc) {

  KosTransferPlan *plan = KosContext_PLAN(ctx);
  ...
}
```

A compact single-line signature does the same when the body is non-trivial,
more than one or two statements. Tight single-statement bodies, such as
macro-based opcode handlers, may omit it.

## Blank lines within bodies

Use blank lines to mark phase boundaries: after a variable declaration block
before the first statement, between sequential `if` / `if-else` guards that
each stand as an independent early exit, after a multi-statement block before
the next unrelated step, and before a `return` that follows a non-trivial
block. Do not insert them mechanically before every statement.

## Negated checks

Use `! ptr`, with a space, rather than `ptr == NULL` or `!ptr`:

```c
/* correct */
if (! val)
  return -1;

if (! PyErr_Occurred())
  PyErr_SetString(...);

/* avoid */
if (val == NULL)
  return -1;
```

## Braces

Single-statement bodies may omit braces. Use them when the body is
multi-statement, or when an `else` branch needs them for symmetry.

```c
if (res < 0)
  return -1;
```

## Section separators

Divide a file into named sections with banner comments, two blank lines
before and one after:

```c
/* === Section name === */
```

## Top-level spacing

Exactly two blank lines between top-level function or variable definitions.

## Core invariant: flattening

A call into Koskari made from Koskari flattens. It installs into the
running evaluator and delivers its result through a resume
(`KosEvalState_CallInto`, `KosEvalState_Call`). Never create a new
context and run a nested `koskari_eval` for it.

A path that fails to flatten is a bug. Any design, comment or document
that describes nesting as acceptable is heresy.

# The end.
