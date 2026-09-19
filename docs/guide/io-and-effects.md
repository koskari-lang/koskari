# I/O and effects

An **effect** is something a program does outside of producing a value: printing,
reading a file, changing a variable, sending a request, or raising an error.

---

## Printing

`print` is the first effect most readers see:

```koskari
(print "hello" "Koskari")
```

At the REPL, expression results are shown for exploration. In scripts, use
`print` when you intentionally want user-visible output.

---

## Files and context managers

Use `with` for resources that need cleanup:

```koskari
with [f (open path)]:
  (print (f.read))
```

The context manager's enter result is bound to the optional name (`f` above),
and cleanup runs when the body exits.

---

## Exceptions

Raise errors when a program cannot continue honestly, and catch errors close to
where you can recover:

```koskari
try:
  (load-config path)
except [e FileNotFoundError]:
  (print "missing config" path)
finally:
  (print "finished config attempt")
```

Use errors for exceptional cases, not ordinary branches.

---

## Async effects

Koskari's evaluator can work with awaitable Python results. Use `await` when you
need explicit control in async code, and keep ordinary beginner scripts
synchronous until an async library requires otherwise.

---

## What you should remember

Values answer “what did this expression produce?” Effects answer “what did this
expression do?” Keep effects visible and intentional.
