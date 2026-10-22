# Script entry point

Run code only when a `.ki` file is executed as a script, not when it is imported
as a module.

## Declaring an entry

```koskari
(declare-entry main)

define main []:
  print "running as script"
```

Save as `tool.ki` and run:

```bash
koskari tool.ki
```

```text
running as script
```

The launcher calls `main` with no arguments after the module has
finished loading. Its return value is the exit status: `None` exits
`0`, an int exits with that status, and any other value is printed to
stderr and exits `1`. Importing `tool` from another module defines
`main` and prints nothing.

## The when-main pattern

`when-main` is the Koskari equivalent of Python's
`if __name__ == "__main__":`. Its body runs as part of loading the
module:

```koskari
define main []:
  print "running as script"

when-main:
  (main)
```

Imported, this module also prints nothing.

## Which to use

| File role | Use |
|------|------------------------|
| Small demo | `when-main`, printing example output for the definitions above it |
| CLI wrapper | `declare-entry`, returning the exit status |
| Web or service entry point | `declare-entry`, starting the server or dispatching to the host framework |

A long-running program belongs in a declared entry. A `when-main` body
that never returns leaves its module's load unfinished, so a module run
from source never reaches the point where its functions are sealed
read-only. The entry runs after that point.

## See also

- [Packaging an installed command](../../../guide/packaging.md)
- [compile/declare-entry](../../../reference/macros/compile-declare-entry.md)
- [compile/when-main](../../../reference/macros/compile-when-main.md)
- [Hello world](../../../quickstart.md)
