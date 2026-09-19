# compile/when-main

**Syntax:** `(when-main BODY ...)`

**Behaviour:** Expands to `(when (equal? __name__ "__main__") (begin BODY ...))`,
so the body runs when the file is executed as a script and not when it
is imported: Python's `if __name__ == "__main__":`.

**Examples:**

```koskari
define main []:
  print "running as a script"

when-main:
  (main)
```

```koskari
>>> (macroexpand-1 '(when-main (main)))
(when (equal? __name__ "__main__") (begin (main)))
```

**See also:** [Modules and packages](../../guide/modules-and-packages.md),
the [Script entry point](../../tutorials/from-python/examples/script-main.md)
example.
