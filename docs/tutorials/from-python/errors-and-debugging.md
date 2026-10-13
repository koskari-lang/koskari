# Errors and debugging

Exceptions are Python exceptions. Tracebacks show Koskari source lines.

## Where an error comes from

:::{dialogue}
Is there a `SyntaxError` and a runtime error, as in Python?
+++
Yes, with a third between them. A reader error, `ReaderSyntaxError`,
means the text could not be read: unbalanced parentheses, a bad block.
A compile error, `CompilerError` and its subclasses, means the form read
but its shape is wrong for the special form at its head. Neither has run
anything. A runtime error is a Python exception raised while running,
from your code or from a library you called.

```koskari
>>> ( 1 2 ]
```

is a reader error,

```koskari
>>> (var)
```

is a compile error, and

```koskari
>>> (/ 1 0)
```

raises `ZeroDivisionError`.
:::

## Handling

:::{pair}
```python
try:
    risky()
except ValueError as e:
    handle(e)
else:
    on_success()
finally:
    cleanup()
```
+++
```koskari
try:
  (risky)
except [ValueError as: e]:
  (handle e)
else:
  (on-success)
finally:
  (cleanup)
```
:::

:::{pair}
```python
try:
    risky()
except:
    handle_anything()
```
+++
```koskari
try:
  (risky)
except:
  (handle-anything)
```
:::

:::{dialogue}
Is `try` a statement?
+++
An expression. Its value is the body's value, or the handler's when one
ran, so `var result: try: ...` is a common shape.
:::

:::{pair}
```python
raise ValueError("bad input")
raise
```
+++
```koskari
raise (ValueError "bad input")
raise
```
:::

## Tracebacks

:::{dialogue}
What does a traceback show?
+++
Koskari frames with their source lines, interleaved with Python frames
where the call crossed into Python, the way a C extension appears in a
Python stack. The REPL trims its own frames; `--repl-full-trace` shows
them.
:::

:::{dialogue}
Does a handler see those frames?
+++
Yes. Inside an `except` clause, `e.__traceback__` holds an entry for
every Koskari frame the exception crossed and for the frame of the
handler, at the line that was executing, so `traceback.format_exc()`,
`logging.exception(...)` and `raise Other() from e` show Koskari source
lines. An exception that escapes to Python carries the same entries.
:::

## Finding the problem

:::{dialogue}
What is the order of attack?
+++
1. Read the exception class and the first message line.
2. Reproduce with one form at the REPL before touching the file.
3. For a macro surprise, `(macroexpand-1 'form)` shows one step of
   expansion.
4. `(help name)` shows what a name is bound to and its documentation.
:::

:::{pair}
```python
print("here", x, x.value)
```
+++
```koskari
print "here" x x.value
```
:::

## Run it

At the REPL:

```koskari
>>> try:
...   (/ 1 0)
... except [ZeroDivisionError]:
...   "caught"
"caught"
```

The exception classes for each phase are in the
[Error guide](../../reference/error-codes.md).
