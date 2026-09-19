# Error guide

Koskari errors currently use exception classes rather than a stable numeric error
code catalog. This page gives the public names you are most likely to see and
how to respond to them.

---

## Reader errors: Koskari could not read the text

Reader errors happen before evaluation starts. They usually mean punctuation,
quoting, or layout needs to be fixed.

| Error | What it means | What to try |
| --- | --- | --- |
| `ReaderSyntaxError` | The reader found invalid syntax, such as mismatched delimiters. | Count `(` / `)`, check brackets, and reread the first error line. |
| `ReaderEOF` | Input ended while a form was still open. | Finish the string, list, block, or closing delimiter. |
| `FormatStringSyntaxError` | A format string could not be parsed. | Check the expression inside the format string. |
| `LayoutError` | An indentation-based block has the wrong shape. | Check the `:` line, indentation, and peer blocks such as `else:`. |

Example:

```koskari
( 1 2 ]
```

This is a reader problem: Koskari cannot turn the text into a form.

---

## Compile errors: Koskari read the form but rejected its shape

Compile errors happen after reading and before runtime evaluation.

| Error | What it means | What to try |
| --- | --- | --- |
| `CompilerError` | Base class for compile-phase failures. | Look for the form shown in the diagnostic. |
| `SpecialError` | A special form has invalid arguments. | Compare the form with its reference page. |
| `SyntaxMatchError` | A form did not match any accepted syntax pattern. | Check argument order, keywords, and required body forms. |
| `NotYetImplemented` | A placeholder feature was reached. | Use a documented form or report the missing case. |

Example:

```koskari
(define)
```

The text is readable, but the `define` form is missing required pieces.

---

## Runtime errors: evaluation started and something went wrong

Runtime failures include ordinary Python exceptions raised by called Python code
and Koskari runtime exceptions.

| Error | What it means | What to try |
| --- | --- | --- |
| `KoskariException` | Base class for Koskari-raised exceptions. | Read the specific subclass and form details. |
| `KoskariSyncViolation` | Async/demand code was used from an incompatible sync path. | Move the operation into an async-aware path or use explicit awaiting. |
| `KoskariContinuationError` | Base class for continuation capture failures. | Read the specific subclass; the two below need different fixes. |
| `KoskariPromptNotFound` | A `shift` found no enclosing delimiter on the dynamic chain. | Establish one: `reset`, or for searches `solve` / `solve-first`. |
| `KoskariPromptUnreachable` | A delimiter exists but a crossed boundary (`boundary` attribute, e.g. a generator) denied the capture. | Restructure so the capture does not cross the boundary; adding a delimiter will not help. |
| Python exceptions such as `ZeroDivisionError`, `NameError`, `ValueError` | A called operation failed at runtime. | Fix the value, missing name, or failing Python call. |

Example:

```koskari
(/ 1 0)
```

This is a runtime problem: the form is readable and compilable, but the operation
cannot succeed.

---

## Reading diagnostics

When an error is long, use this order:

1. Read the exception class name.
2. Read the first message line.
3. If a source line and caret are shown, fix that location first.
4. If a form is shown, compare that form to the relevant reference page.
5. Change one thing and rerun.

See also [Errors and debugging](../tutorials/from-python/errors-and-debugging.md)
for a Python-oriented walkthrough.
