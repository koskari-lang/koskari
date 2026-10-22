# compile/entry-point

**Syntax:** `@entry-point` over a `define`, or `(decorate [entry-point] (define NAME FORMALS BODY ...))`

**Behaviour:** An arm of [`decorate-subject`](compile-decorate-subject.md).
It binds the decorated function as the module's `__entry__` and delivers
the function unchanged, so `NAME` is bound as it would be without the
decorator. When the module runs as a program -- a script, `-m`, `-c`,
source on stdin, a packaged `koskari_scripts` command, or `python -m` --
the launcher calls `__entry__` with no arguments once the module has
finished loading, and its return value becomes the process exit status:

| Return value | Exit status |
|---|---|
| `None` | `0` |
| an `int` | that int |
| anything else | `1`, after the value is printed to stderr |

A `SystemExit` raised inside the entry propagates, so `sys.exit` keeps
its meaning. An imported module's entry is never called. A module with
no entry exits `0` once it has loaded.

`__entry__` is the contract: the decorator is one way to write it, and a
program may define the function under that name directly.

```koskari
define __entry__ []:
  print "the same program"
```

The entry takes no arguments; read command-line arguments from
`sys.argv`, or give the entry a parameter whose default reads them. A
coroutine function works as an entry: its result is awaited on the
running event loop before the status is taken. The module finishes
loading, and its functions are sealed read-only, before the entry runs.
Under `-i`, the entry runs and the session starts once it returns; its
return value does not end the session.

**Examples:**

```koskari
;; tool.ki
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

`main` stays an ordinary function: another module can import and call
it, and a test can call it with its own arguments.

**See also:** [`decorate`](compile-decorate.md),
[`declare-exports`](compile-declare-exports.md),
[`when-main`](compile-when-main.md), the
[CLI reference](../cli-and-config.md), and the
[Script entry point](../../tutorials/from-python/examples/script-main.md)
example.
