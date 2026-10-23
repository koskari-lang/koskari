# compile/when-main

**Syntax:** `(when-main BODY ...)`

**Behaviour:** Expands to `(when (equal? __name__ "__main__") (begin BODY ...))`,
Python's `if __name__ == "__main__":`. The body runs as part of loading,
while the module is still open, and only when the file is the program.

A program's work belongs in an entry function, not in a body that runs
during the load: see [`entry-point`](compile-entry-point.md). The entry
runs after the module has finished loading, with its return value as the
exit status, and it stays an ordinary function that can be imported and
called.

```koskari
>>> (macroexpand-1 '(when-main (main)))
(when (equal? __name__ "__main__") (begin (main)))
```

**See also:** [`entry-point`](compile-entry-point.md),
[Modules and packages](../../guide/modules-and-packages.md).
