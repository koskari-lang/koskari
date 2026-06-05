# Error codes

Koskari currently documents error behavior by **exception class** and message,
not by a stable public numeric or string error-code catalog.

## What users should look at first

When an error appears, start with the first useful line of the message:

- **Reader or syntax errors** mean Koskari could not turn the text into forms.
  Check parentheses, brackets, quotes, and indentation.
- **Compiler errors** mean the text was readable, but a form did not match the
  language rules. Check the syntax of the special form or macro you are using.
- **Runtime errors** mean evaluation started and something failed while the
  program was running. Check values, missing names, item access, division, and
  calls into Python objects.

## Common examples

```koskari
( 1 2 ]
```

This is a reader/syntax problem: the brackets do not match.

```koskari
(/ 1 0)
```

This is a runtime problem: the program is valid text, but the operation fails.

```koskari
(define)
```

This is a compiler problem: `define` is a known form, but it was not given the
pieces its syntax requires.

## API reference

For Python code embedding or extending Koskari, see the exception classes in the
[API reference](../api/koskari-exceptions.md). User-facing documentation should
prefer exception names and clear messages until stable public error identifiers
are introduced.
