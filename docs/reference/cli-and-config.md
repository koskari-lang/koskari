# CLI and configuration

The `koskari` command runs scripts and the REPL.

## Usage

```
koskari [options] [-c CMD | filename] [args ...]
```

## Options

| Flag | Description |
|------|-------------|
| `--no-tweak-path` | Do not add the current directory to `sys.path` |
| `--histfile PATH` | REPL readline/prompt_toolkit history file |
| `--repl-full-trace` | Show REPL infrastructure frames in error tracebacks |
| `-i`, `--interactive` | Enter REPL after running a script |
| `-c CMD`, `--command CMD` | Run koskari source from CMD, then exit |
| `--compile-kio OUT SRC` | Compile `SRC` `.ki` to `.kio` at `OUT`, then exit |
| `--compile-kio-source-name PATH` | Logical source path stored in `.kio` |
| `--execute-kio SRC` | Run precompiled `.kio` at `SRC` |

Exactly one run target is required for script execution: `-c CMD`,
`--compile-kio OUT SRC`, `--execute-kio SRC`, or positional `filename`.
Those forms are mutually exclusive. Remaining arguments are forwarded to the
script or REPL session; with `-c`, `sys.argv[0]` is `'-c'`.

## Related commands

| Command | Purpose |
|---------|---------|
| `koskari-lsp` | Language server (LSP) |
| `python -m koskari` | Same as `koskari` |
| Installed app commands | Declared with **`koskari_scripts`**; see [Packaging an installed command](../patterns/recipes/packaging-installed-scripts.md) |

## Environment

Test and debug environment variables are documented in
[Build and test](../project/build-test.md) (`KOS_TRACE_*`, `DEBUG`, etc.).
