# Types or contracts

Koskari can record type annotations on functions and parameters, and it runs on
Python values at runtime. Treat annotations as reader and tooling aids unless a
specific API says it enforces a contract.

---

## Return annotations

Use `::` or `type:` on a function definition to record a return annotation:

```koskari
(define answer :: int []
  42)

(define greeting type: str [name]
  (+ "Hello, " name))
```

The annotation is metadata on the compiled function. It helps documentation,
inspection, and interop.

## Parameter annotations

Parameters may also carry annotations:

```koskari
(define repeat-message [message :: str count :: int]
  (for-each [i (range count)]
    (print message)))
```

Use annotations to communicate intent. Do not use them as a substitute for
clear names or simple validation.

## Runtime checks

For explicit runtime questions, use predicates and comparisons:

```koskari
(instance? value str)
(eq status "ready")
(in name allowed-names)
```

These are ordinary expressions, so they work inside `if`, `cond`, `when`, and
function bodies.

## A practical pattern

For user-facing inputs, validate near the edge of your program:

```koskari
(define print-count [count :: int]
  (unless (gt count -1)
    (raise (ValueError "count must be non-negative")))
  (print count))
```

The annotation documents the expected shape; the explicit check gives the
runtime behavior.

## What you should remember

- Annotations are useful metadata for readers and tools.
- Runtime behavior should still be expressed with ordinary checks when it
  matters.
- Keep error messages close to the boundary where bad input enters.

Next: [I/O and effects](09-io-and-effects.md).
