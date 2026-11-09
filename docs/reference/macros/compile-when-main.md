# compile/when-main

**Syntax:** `(when-main BODY ...)`

**Behaviour:** Runs the body forms in order when the module's `__name__`
equals `"__main__"`, as Python's `if __name__ == "__main__":`. The body runs as part of loading,
while the module is still open, and only when the file is the program.

A program's work belongs in an entry function, not in a body that runs
during the load: see [`entry-point`](compile-entry-point.md). The entry
runs after the module has finished loading, with its return value as the
exit status, and it stays an ordinary function that can be imported and
called.

```koskari
def main []:
  print "running as the program"

(when-main (main))   ;; calls main only when the file is the program
```

**See also:** [`entry-point`](compile-entry-point.md),
[Modules and packages](../../guide/modules-and-packages.md).
