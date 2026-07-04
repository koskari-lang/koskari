# Errors and debugging

Python developers expect tracebacks, exception types, and a REPL for
reproduction. Koskari aligns with that — with Koskari source lines in the stack
when available.

---

## Two failure phases

> **Q:** SyntaxError vs ValueError — same split?
>
> **A:** Same idea:
>
> - **Read/parse failure** — bad parentheses, invalid layout headers. Koskari
>   never starts evaluating.
> - **Runtime failure** — form read fine, execution hit a problem (division by
>   zero, missing name, Python exception from interop).

> **Q:** REPL example?
>
> **A:**
>
> ```koskari
> koskari> ( 1 2 ]
> ```
>
> Syntax error → stderr, often “mismatched” delimiters.
>
> ```koskari
> koskari> (/ 1 0)
> ```
>
> Runtime error → **`ZeroDivisionError`** (a Python exception).

---

## Exception handling

> **Q:** `try` / `except` / `else` / `finally`?
>
> **A:** Layout form (preferred in files):
>
> ```koskari
> try:
>   (risky-call)
> except [ValueError as: e]:
>   (handle e)
> else:
>   (on-success)
> finally:
>   (cleanup)
> ```
>
> Bare catch-all:
>
> ```koskari
> try:
>   (risky-call)
> except:
>   (handle-anything)
> ```
>
> Bind the exception with `except [e ValueError]:` or
> `except [ValueError as: e]:`. See [layout/try](../../reference/layouts/layout-try.md)
> and [try](../../reference/specials/compile-try.md).

> **Q:** Parenthesized form?
>
> **A:**
>
> ```koskari
> (try (risky-call)
>   [[e ValueError] (handle e)]
>   [else: (on-success)]
>   [finally: (cleanup)])
> ```

> **Q:** Re-raise?
>
> **A:** **`raise`** — propagate the current or a new exception.

---

## Tracebacks

> **Q:** Will I see Koskari source lines?
>
> **A:** Yes when line tables are present — Koskari tracebacks interleave
> Koskari and Python frames, similar to C extensions in Python stacks.

> **Q:** REPL noise?
>
> **A:** By default the REPL **trims infrastructure frames** so your expression
> stands out. Pass **`--repl-full-trace`** for the full stack (see
> [CLI and configuration](../../reference/cli-and-config.md)).

---

## Debugging workflow

> **Q:** Practical steps when something fails?
>
> **A:**
>
> 1. **Classify** — syntax (fix punctuation or layout headers) vs runtime.
> 2. **Reproduce small** — one form at the REPL before debugging a whole file.
> 3. **Read the first useful line** — exception type and message, like Python.
> 4. **Inspect expansion** — for macro surprises, `(macroexpand-1 'form)` at
>    the REPL.
> 5. **Disassemble** — `(help fn)` or `koskari.dis` from Python when behavior
>    does not match source.

> **Q:** `print` debugging?
>
> **A:** Still valid:
>
> ```koskari
> print:
>   "here"
>   x
>   (attr x "value")
> ```

---

## Compile-time errors

> **Q:** Can code fail before runtime?
>
> **A:** Yes — unknown specials, bad macro expansion, or arity errors at compile
> time surface as compiler errors with form context. Fix the source; `try` does
> not catch a failed **load** of broken syntax.

---

## Try it

At the REPL, trigger a handled error:

```koskari
koskari> try:
     ...     (/ 1 0)
     ... except [ZeroDivisionError]:
     ...     "caught"
     ...
```

You should see `"caught"` instead of an uncaught traceback.

---

## Remember

Syntax errors are reader failures; many runtime errors are Python exceptions.
Use layout `try` / `except` peers like Python. REPL, `macroexpand-1`, and
`--repl-full-trace` help when stacks feel opaque.

Next: [Interop](interop.md).
