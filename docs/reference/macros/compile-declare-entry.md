# compile/declare-entry

**Syntax:** `(declare-entry NAME)`

**Behaviour:** Expands to `(define-global __entry__ "NAME")`. When the
module runs as a program -- a script, `-m`, `-c`, or source on stdin --
the launcher calls the function bound to `NAME` with no arguments once
the module has finished loading. Its return value becomes the process
exit status:

| Return value | Exit status |
|---|---|
| `None` | `0` |
| an `int` | that int |
| anything else | `1`, after the value is printed to stderr |

An imported module's entry is never called. The name is recorded as a
string, so the declaration may come before the definition. A declared
name that is not bound when the entry would run raises `NameError`.

The entry takes no arguments; read command-line arguments from
`sys.argv`, or give the entry a parameter whose default reads them.

The module finishes loading, and its functions are sealed read-only,
before the entry runs. A `when-main` body, by contrast, runs as part
of loading; if it completes, the declared entry still runs afterward.
Under `-i`, the entry runs and the session starts once it returns; its
return value does not end the session.

**Examples:**

```koskari
;; tool.ki
import sys

(declare-entry main)

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

**See also:** [`when-main`](compile-when-main.md),
[`declare-exports`](compile-declare-exports.md), the
[Script entry point](../../tutorials/from-python/examples/script-main.md)
example.
