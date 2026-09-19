# CLI and configuration

The `koskari` command runs scripts and the REPL.

## Usage

```
koskari [options] [-c CMD | filename | -] [args ...] [-- args ...]
```

When running a script, use `--` before arguments that look like options
(start with `-`). Everything after `--` is forwarded to the script's
`sys.argv` unchanged. Positional arguments may appear before `--` as well;
for example, `koskari app.ki 20 -- --verbose` sets `sys.argv` to
`['app.ki', '20', '--verbose']`.

## Options

| Flag | Description |
|------|-------------|
| `-h`, `--help` | Show the usage message, then exit |
| `--version` | Print Koskari and Python version information, then exit |
| `-q`, `--quiet` | Suppress the REPL startup version banner |
| `--no-tweak-path` | Do not add the current directory to `sys.path` |
| `--histfile PATH` | REPL readline/prompt_toolkit history file |
| `--repl-full-trace` | Show REPL infrastructure frames in runtime error tracebacks |
| `-i`, `--interactive` | Enter interactive mode after executing the given script |
| `-c CMD`, `--command CMD` | Evaluate s-expressions from CMD, then exit |
| `--compile-kio OUT SRC` | Compile `SRC` `.ki` to `.kio` at `OUT`, then exit |
| `--compile-kio-source-name PATH` | Logical `.ki` path stored in the `.kio` for tracebacks; defaults to the input path |
| `--execute-kio SRC` | Run `SRC` as a precompiled `.kio` |

Exactly one run target is required for script execution: `-c CMD`,
`--compile-kio OUT SRC`, `--execute-kio SRC`, or positional `filename`.
Those forms are mutually exclusive. Remaining arguments are forwarded to the
script or REPL session; with `-c`, `sys.argv[0]` is `'-c'`.

A `filename` of `-` reads the whole program from stdin, so a heredoc or a
pipe runs the same source a file would:

```
koskari - <<'EOF'
define greet [who]:
  print f"hi {who}"
greet "stdin"
EOF
```

## Layout and `-c`

`-c` reads **s-expressions**; files, stdin via `-`, and the REPL read
**layout**. The difference matters because a layout line is a single form, so
several expressions on one line become one implicit call: `+ 1 2 3` means
`(+ 1 2 3)`, and `logger.getLogger "foo"` means `((attr logger getLogger)
"foo")`. That is what you want in a program and not what you want in a
one-liner, where consecutive top-level forms are the natural reading:

```
koskari -c '(print "a") (print "b")'    # prints a, then b
```

Read as layout, that same line would be `((print "a") (print "b"))` -- both
forms evaluate, then the result of the first is called. For a layout program
from the command line, use `-`.

## Related commands

| Command | Purpose |
|---------|---------|
| `koskari-lsp` | Language server (LSP) |
| `python -m koskari` | Same as `koskari` |
| Installed app commands | Declared with **`koskari_scripts`**; see [Packaging an installed command](../guide/packaging.md) |

## Environment

Test and debug environment variables are documented in
[Build and test](../project/build-test.md) (`KOS_TRACE_*`, `DEBUG`, etc.).
