# Error guide

Koskari errors currently use exception classes rather than a stable numeric error
code catalog. This page gives the public names you are most likely to see and
how to respond to them.

Every class on this page except `KoskariContinuationDead` is bound in
every module, as is every Python exception type, so each can be raised,
caught, and tested by name with no import:

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
  RuntimeError
    KoskariContinuationDead
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
| `KoskariContinuationError` | Base class for continuation capture and transfer failures. It is not a `RuntimeError` subclass, so an `except RuntimeError` around a continuation-using call will not swallow it; `KoskariPromptUnreachable` is the one subclass that is. | Read the specific subclass; its two subclasses below need different fixes. |
| `KoskariPromptNotFound` | A `shift` or `abort` found no enclosing `reset` (`shift has no enclosing reset`, `abort has no enclosing reset`), or a `shift-at` or `abort-at` found no enclosing `reset-at` carrying its tag (`shift-at has no enclosing reset-at carrying its tag`, and the same for `abort-at`); the `tag` attribute is the tag sought. | Establish one: `reset` for `shift` and `abort`, `reset-at` with the same tag for `shift-at` and `abort-at`, or for searches `solve` / `solve-first`. |
| `KoskariPromptUnreachable` | A boundary denied a capture or an abort (`boundary` attribute): a generator, or for a capture a running `enter`, `exit`, `before` or `after` call of a `with` or `dyn-wind`; for `shift` and `abort` a delimiter exists beyond it. `operation` says which construct was refused. Unlike the rest of its category it is also a `RuntimeError`. | Restructure so the capture or abort does not cross the boundary; adding a delimiter will not help. |
| `KoskariContinuationDead` | A full continuation (from `call/cc`, `let/cc` or `capture/cc`) was invoked while the evaluation that captured it was not running: it had returned, or it was suspended on an `await`. Each top-level form of a module runs as its own evaluation, so a continuation captured by one top-level form is dead in the next. Invoking it from Koskari code or from Python raises the same error, with the message `continuation target context is no longer alive`. It is a `RuntimeError`, not a `KoskariContinuationError`, and is not bound in modules: `(import-from koskari.lib KoskariContinuationDead)`. | Capture and invoke the continuation within one running evaluation, such as one function body, not across top-level forms or a suspension. |
| Python exceptions such as `ZeroDivisionError`, `NameError`, `ValueError` | A called operation failed at runtime. | Fix the value, missing name, or failing Python call. |

### Koskari errors that subclass a Python type

Three Koskari classes inherit from a Python exception type.

| Error | Python base | What it means | What to try |
| --- | --- | --- | --- |
| `KoskariSyncViolation` | `TypeError` | A synchronous demand path reached an `await` or a `yield` it cannot perform. | Move the operation into an async-aware path, or stop demanding the call. |
| `KoskariContinuationDead` | `RuntimeError` | A full continuation was invoked while the evaluation that captured it was not running. | See the runtime table above. |
| `KoskariSyntaxError` | `SyntaxError` | Read-time or compile-time syntax failure; the base of the reader errors above. | See the reader table. |

`KoskariSyncViolation` is defined in the native extension and inherits from
`TypeError` alone, so an `except KoskariException` will not catch it; catch it
by its own name or by `TypeError`. When the demand path reaches an `await` on
a Python awaitable, the violation is thrown into that awaitable, so a Python
`except TypeError` around that `await` catches it. `KoskariContinuationDead`
is likewise defined in the native extension and inherits from `RuntimeError`
alone, so neither `except KoskariException` nor
`except KoskariContinuationError` catches it; catch it by its own name or by
`RuntimeError`.
`KoskariSyntaxError` inherits from both `SyntaxError` and `KoskariException`.

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
