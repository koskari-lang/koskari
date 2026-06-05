# I/O and effects

An **effect** is something a program does besides producing a value: printing,
reading a file, changing a list, importing a module, raising an exception, or
awaiting asynchronous work.

---

## Values and effects

This expression mainly produces a value:

```koskari
(+ 20 22)
```

This expression mainly performs an effect:

```koskari
(print "saved")
```

Good programs often use both. Keep calculations easy to test by separating pure
value-producing helpers from effectful edges.

## Printing

`print` is the first effect most programs use:

```koskari
(print "total:" total)
```

It is useful for scripts, examples, and quick debugging. For reusable functions,
prefer returning a value and letting the caller decide whether to print it.

## Imports are effects too

Importing loads a module and creates bindings:

```koskari
(import os.path as: path)
```

Keep imports near the top of files so readers can see external dependencies
quickly.

## Exceptions

Use `raise` when a program cannot continue normally:

```koskari
(raise (ValueError "expected a non-empty name"))
```

Use `try` forms when you can recover or translate an error into a better message
for your caller.

## Async and `await`

Koskari has explicit `await` support for awaitable values and deferred work.
Use it at the point where your program needs the result:

```koskari
(await pending-result)
```

If you are writing synchronous beginner scripts, you can ignore async until you
call APIs that return awaitables.

## What you should remember

- Effects change the world around the expression.
- Return values from helpers; perform I/O at the edges.
- Use exceptions for failures callers should notice.
- Use `await` only when working with awaitable or deferred values.

Next: [Testing and quality](10-testing-and-quality.md).
