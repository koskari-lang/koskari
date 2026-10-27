# Error guide

Koskari errors currently use exception classes rather than a stable numeric error
code catalog. This page gives the public names you are most likely to see and
how to respond to them.

Every class on this page is bound in every module, as is every Python
exception type, so each can be raised, caught, and tested by name with
no import:

```koskari
(try (shift k 1)
  [[e KoskariContinuationError] (type e).__name__])   ; "KoskariPromptNotFound"
```

## Hierarchy

```text
Exception
  KoskariException
    NotYetImplemented
    KoskariSyntaxError      (also SyntaxError)
      ReaderError
        ReaderSyntaxError
          FormatStringSyntaxError
        ReaderEOF
        LayoutError
    CompilerError
      SpecialError
        SyntaxMatchError
      FenceError
      ParametersError
      UncaughtCompilerError
    KoskariContinuationError
      KoskariPromptNotFound
      KoskariPromptUnreachable
  TypeError
    KoskariSyncViolation
```

Compile errors are not `SyntaxError`s: `[[SyntaxError] ...]` catches a
reader error but not a `SyntaxMatchError`; catch `CompilerError`.

---

## Reader errors: Koskari could not read the text

Reader errors happen before evaluation starts. They usually mean punctuation,
quoting, or layout needs to be fixed.

| Error | What it means | What to try |
| --- | --- | --- |
| `ReaderError` | Base class for reader-phase failures. It subclasses `KoskariSyntaxError`, which in turn subclasses Python's `SyntaxError`. | Catch this to handle any reader failure in one place. |
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
| `SyntaxMatchError` | A form matched no arm of a special's syntax. The diagnostic lists the patterns that special accepts. | Check argument order, keywords, and required body forms. |
| `ParametersError` | A parameter or argument list is malformed: `*` after keyword arguments, a keyword with no value, or a positional argument after a keyword. | Reorder the arguments and give every keyword a value. |
| `FenceError` | A fence block could not be expanded, or the foreign source inside it could not be parsed. | Check the fence's options and the embedded source. |
| `UncaughtCompilerError` | An unexpected exception escaped the compiler and was wrapped with the form being compiled. | Read the chained cause. Report it when the form is valid. |

Example:

```koskari
(var)
```

The text is readable, but the `var` form is missing required pieces.

---

## Runtime errors: evaluation started and something went wrong

Runtime failures include ordinary Python exceptions raised by called Python code
and Koskari runtime exceptions.

| Error | What it means | What to try |
| --- | --- | --- |
| `KoskariException` | Base class for Koskari-raised exceptions. | Read the specific subclass and form details. |
| `NotYetImplemented` | A placeholder for a feature that is not implemented yet. | Use a documented form or report the missing case. |
| `KoskariContinuationError` | Base class for continuation capture and transfer failures. It is not a `RuntimeError` subclass, so an `except RuntimeError` around a continuation-using call will not swallow it. | Read the specific subclass; the two below need different fixes. |
| `KoskariPromptNotFound` | A `shift` found no enclosing delimiter on the dynamic chain. | Establish one: `reset`, or for searches `solve` / `solve-first`. |
| `KoskariPromptUnreachable` | A delimiter exists but a crossed boundary (`boundary` attribute, e.g. a generator) denied the capture. | Restructure so the capture does not cross the boundary; adding a delimiter will not help. |
| Python exceptions such as `ZeroDivisionError`, `NameError`, `ValueError` | A called operation failed at runtime. | Fix the value, missing name, or failing Python call. |

### Koskari errors that subclass a Python type

Two Koskari classes inherit from a Python exception type.

| Error | Python base | What it means | What to try |
| --- | --- | --- | --- |
| `KoskariSyncViolation` | `TypeError` | A synchronous demand path reached an `await` or a `yield` it cannot perform. | Move the operation into an async-aware path, or stop demanding the call. |
| `KoskariSyntaxError` | `SyntaxError` | Read-time or compile-time syntax failure; the base of the reader errors above. | See the reader table. |

`KoskariSyncViolation` is defined in the native extension and inherits from
`TypeError` alone, so an `except KoskariException` will not catch it; catch it
by its own name or by `TypeError`. `KoskariSyntaxError` inherits from both
`SyntaxError` and `KoskariException`.

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
