# compile/try
**Symbol:** `compile/try`

**Syntax:** `(try BODY CLAUSE...)`

where each `CLAUSE` is `(EXCEPTMATCH BODY...)` and `EXCEPTMATCH` is one of:

- `[TYPEEXPR]`, `[SYM TYPEEXPR]`, or `[TYPEEXPR as: SYM]` -- USE form; type-only
or binding.
- `except:` -- bare catch-all; catches any exception without a type check or binding

In addition, two keyword-head clauses are accepted:

- `(else: BODY...)` -- run when the try body completed without exception
- `(finally: BODY...)` -- always run; must be last

**Use-case:** Exception handling. Runs BODY; on exception, matches against clauses in order and runs the first matching handler. Optional `else:` runs when no exception occurred. Optional `finally:` runs on every exit from the form: normal completion, an exception, a `return`, `break` or `continue`, and a continuation transfer that leaves it (a `call/cc` escape, or a `shift` handler that abandons the captured slice), in order with the `__exit__` of any `with` and the after thunk of any `dyn-wind` on the same path, innermost first. A slice re-entered through its continuation runs the `finally:` again when that run leaves. An escape issued from inside a running `finally:` body abandons the rest of that body. At least one except-style clause or `finally:` is required.

**Behaviour:** Uses PUSH_TRY, CLEAR_TRY, POP_TRY, PUSH_FINALLY, POP_FINALLY.
Exception handlers use EXCEPTION_MATCH, EXCEPTION_CLEAR, EXCEPTION opcodes.
The bare `except:` form is lowered to an `else:` clause in the internal
`compile/cond` dispatch so no EXCEPTION_MATCH is emitted; uncaught exceptions
re-raise via compile/raise unless a bare handler is present. `else:` runs
between CLEAR_TRY and POP_TRY; `finally:` runs after cleanup.  Expressions in
`finally:` are compiled for side effects only; they do not replace the value
of the try form (including a pending `compile/return` from the try or except
paths).

**Examples:**

```koskari
;; Type-only catch
(try (risky-call)
  [[Exception] (handle-exception)])

;; Named binding
(try (parse input)
  [[e ValueError] (log e)])

;; Bare catch-all
(try (risky-call)
  [except: (handle-anything)])

;; Try/else/finally
(try (do-work)
  [else: (success)]
  [finally: (cleanup)])

;; Full form
(try (do-work)
  [[Exception] (handle-exc)]
  [else: (on-success)]
  [finally: (cleanup)])
```
