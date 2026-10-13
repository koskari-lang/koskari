# Script entry point

A program's work lives in one function, marked as the entry point. It
runs when the file is executed as a program, not when it is imported as a
module.

## Marking the entry point

```koskari
@entry-point
def main []:
  print "running as a program"
```

Save as `tool.ki` and run:

```bash
koskari tool.ki
```

```text
running as a program
```

The launcher calls `main` with no arguments after the module has
finished loading. Its return value is the exit status: `None` exits
`0`, an int exits with that status, and any other value is printed to
stderr and exits `1`. Importing `tool` from another module defines
`main` and prints nothing, and the importer can call `main` itself.

## Arguments and status

The entry takes no arguments; read them from `sys.argv`, or let a
parameter's default do it:

```koskari
import sys

@entry-point
define main [args: (item-slice sys.argv 1)]:
  if args:
    print "hello," args[0]
  else:
    print "usage: tool NAME"
    2
```

```bash
koskari tool.ki world     # prints "hello, world", exits 0
koskari tool.ki           # prints the usage line, exits 2
```

A function that calls `sys.exit` itself keeps working: the exit passes
through the launcher unchanged.

## Compared with Python

Python runs a program's work from a block guarded by
`if __name__ == "__main__":`, which executes while the module is still
loading. Koskari has the same guard as `when-main`, and it runs at the
same time, but a program's work does not belong there: a body that runs
during the load cannot return a status, is not a function anything else
can call, and keeps the module's load unfinished for as long as it runs.
The entry point runs after the load completes, so the module is sealed
and every definition is in place when the program starts.

## See also

- [Packaging an installed command](../../../guide/packaging.md)
- [compile/entry-point](../../../reference/macros/compile-entry-point.md)
- [compile/when-main](../../../reference/macros/compile-when-main.md)
- [Hello world](../../../quickstart.md)
