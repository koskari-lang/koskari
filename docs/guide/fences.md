# Fences

A fence is a triple-backtick block that captures raw source in a foreign
format and lowers it, at compile time, to Koskari source:

````text
```bash
echo hello
```
````

The opener names the format and the body is captured verbatim. The
reader does not read the body; it wraps it as `(apply-fence NAME RAW
META)`. Compilation resolves a handler named `fence/<name>` and asks it
for Koskari source, which is compiled in place of the fence.

A fence handler is a compile-time transliterator, not a runtime parser.
The shape is the same as layout expansion: the reader captures
structure, and a compile-time step turns it into ordinary source before
the rest of compilation runs. The dispatch is
[`compile/apply-fence`](../reference/specials/compile-apply-fence.md);
the built-in handler and how to write one are in the
[fence reference](../reference/fences/index.md).

## When a format earns a fence

The bar is that the notation is awkward to express in Koskari, not that
it looks different. The built-in `fence/bash` clears it: "capture a
shell script, run it asynchronously, and hand back a result object" is
clumsy as s-expressions, reads cleanly at the call site, and fits
process-driven work such as a job scheduler.

Data notations mostly do not clear it. A JSON fence was prototyped and
dropped: `{ "a": 1 }` differs from `#{"a": 1}` by two characters, and
the native literal gives the compiler structure a foreign parser
cannot. Where native syntax expresses the value, a fence adds a second
grammar. A fence is for the case where the foreign form is the clear
one, a shell script or a large template body.

## Turning fences off

The `reader/fences` pragma, on by default, governs fence reading. A
module that wants the backtick to keep only its quote and quasiquote
readings turns it off:

```koskari
(pragma [reader/fences False])
```

See [Pragmas and feature settings](pragma.md).
