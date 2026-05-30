# Errors and debugging

Python developers expect tracebacks, exception types, and a REPL for
reproduction. Koskari aligns with that — with a few Koskari-specific frames.

---

## Two failure phases

> **Q:** SyntaxError vs ValueError — same split?
>
> **A:** Same idea:
>
> - **Read/parse failure** — bad parentheses, invalid tokens. Koskari never
>   starts evaluating.
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
> Runtime error → stderr with **`ZeroDivisionError`** (a Python exception).

---

## Exception handling

> **Q:** `try` / `except` / `else` / `finally`?
>
> **A:**
>
> ```koskari
> (try (risky-call)
>   [[ValueError] (handle e)]
>   [else: (on-success)]
>   [finally: (cleanup)])
> ```
>
> Clauses use bracket patterns: `[[Type] body]`, `[[e Type] body]`, or bare
> `[except: body]`. **`else:`** runs when no exception occurred; **`finally:`**
> always runs. See [try](../../reference/specials/compile-try.md).

> **Q:** Re-raise?
>
> **A:** **`raise`** — propagate the current or a new exception.

> **Q:** Catch everything?
>
> **A:**
>
> ```koskari
> (try (risky-call)
>   [except: (handle-anything)])
> ```

---

## Tracebacks

> **Q:** Will I see Koskari source lines?
>
> **A:** Yes when line tables are present — Koskari tracebacks interleave
> Koskari and Python frames, similar to C extensions in Python stacks.

> **Q:** REPL noise?
>
> **A:** By default the REPL **trims infrastructure frames** so your expression
> stands out. Pass **`--repl-full-trace`** if you need the full stack (see
> [CLI and configuration](../../reference/cli-and-config.md)).

> **Q:** Koskari-specific exceptions?
>
> **A:** Some errors use Koskari exception types (`koskari.exceptions`) —
> especially at compile or boundary layers. Message text still points at the
> offending form when possible.

---

## Debugging workflow

> **Q:** Practical steps when something fails?
>
> **A:**
>
> 1. **Classify** — syntax (fix punctuation) vs runtime (fix logic or types).
> 2. **Reproduce small** — one form at the REPL before debugging a whole file.
> 3. **Read the first useful line** — exception type and message, like Python.
> 4. **Inspect expansion** — for macro surprises, `(macroexpand-1 'form)` at
>    the REPL (bootstrap runtime).
> 5. **Disassemble** — `(help fn)` or Python `koskari.dis` for bytecode when
>    behavior does not match source.

> **Q:** `print` debugging?
>
> **A:** Still valid:
>
> ```koskari
> (print "here" x (attr x "value"))
> ```

---

## Compile-time errors

> **Q:** Can code fail before runtime?
>
> **A:** Yes — unknown special forms, bad macro expansion, arity errors at
> compile time surface as **`SpecialError`** / compiler errors with form
> context. Fix the source form; no amount of runtime `try` catches a compile
> failure for that load.

---

## What you should remember

Syntax errors are reader failures; runtime errors are Python-compatible
exceptions in many paths. `try` mirrors Python’s structure with bracket clause
syntax. Use the REPL, `macroexpand-1`, and `--repl-full-trace` when stacks
feel opaque.

Next: [Interop](interop.md).
