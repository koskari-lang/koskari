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

## Examples in the repository

| File | What `when-main` does |
|------|------------------------|
| [examples/dataclass.ki](../../../examples/dataclass.ki) | Demo prints for `Point` |
| [examples/fastapi/fastapi.ki](../../../examples/fastapi/fastapi.ki) | Starts or drives the demo server |
| [examples/life/koslife/cli.ki](../../../examples/life/koslife/cli.ki) | Life game CLI |

## See also

- [compile/when-main](../../reference/macros/compile-when-main.md)
- [Hello world](hello.md)
