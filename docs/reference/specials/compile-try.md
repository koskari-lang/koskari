# compile/try
**Symbol:** `compile/try`

**Syntax:** `(try BODY CLAUSE...)`

where each `CLAUSE` is `(EXCEPTMATCH BODY...)` and `EXCEPTMATCH` is one of:

- `[TYPEEXPR]`, `[SYM TYPEEXPR]`, or `[TYPEEXPR as: SYM]` -- USE form; type-only
or binding.
- `except:` -- bare catch-all; catches any exception without a type check or binding

In addition, two keyword-head clauses are accepted:

- `(else: BODY...)` -- run when the try body completed without exception
- `(finally: BODY...)` -- run on every local exit; must be last

**Use-case:** Exception handling. Runs BODY; on exception, matches against clauses in order and runs the first matching handler. Optional `else:` runs when no exception occurred. Optional `finally:` runs on every local exit from the form: normal completion, an exception, and a `return`, `break` or `continue` written in it. A continuation transfer does not run it: a `call/cc` escape out of the try, or a `shift` that aborts the region the try is in, leaves the `finally:` unrun. Use [`dyn-wind`](compile-dyn-wind.md) for cleanup that a transfer must run. A slice re-entered through its continuation runs a copy of the try, and that copy runs the `finally:` when it exits locally. An escape issued from inside a running `finally:` body abandons the rest of that body. At least one except-style clause or `finally:` is required.

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

**Also:** layout handler [`try`](../layouts/layout-try.md).
