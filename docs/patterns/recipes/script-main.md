# Script entry point

Run code only when a `.ki` file is executed as a script, not when it is imported
as a module — the Koskari equivalent of Python’s `if __name__ == "__main__":`.

## Prerequisites

- [Install and run](../../start/install-run.md)
- [From Python: Programs and modules](../../start/from-python/programs-and-modules.md)

## Minimal pattern

```koskari
define main []:
  print "running as script"

when-main:
  (main)
```

Save as `tool.ki` and run:

```bash
koskari tool.ki
```

## When to use it

Use `when-main:` for code that should run only from the command line:

| File role | What `when-main` usually does |
|------|------------------------|
| Small demo | Prints example output for the definitions above it |
| CLI wrapper | Parses arguments and calls a `main` function |
| Web or service entry point | Starts the server or dispatches to the host framework |

## See also

- [compile/when-main](../../reference/macros/compile-when-main.md)
- [Hello world](hello.md)
