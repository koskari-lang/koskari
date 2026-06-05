# CLI and configuration

The `koskari` command runs scripts and the REPL.

## Usage

```
koskari [options] [filename] [args ...]
```

## Options

| Flag | Description |
|------|-------------|
| `--no-tweak-path` | Do not add the current directory to `sys.path` |
| `--histfile PATH` | REPL readline/prompt_toolkit history file |
| `--repl-full-trace` | Show REPL infrastructure frames in error tracebacks |
| `-i`, `--interactive` | Enter REPL after running a script |
| `--compile-kio OUT` | Compile `.ki` to `.kio` at OUT, then exit |
| `--compile-kio-source-name PATH` | Logical source path stored in `.kio` |
| `--execute-kio` | Run filename as precompiled `.kio` |

Positional `filename` runs as a script. Remaining arguments are forwarded to
the script or REPL session.

## Related commands

| Command | Purpose |
|---------|---------|
| `koskari-lsp` | Language server (LSP) |
| `python -m koskari` | Same as `koskari` |

## Environment

Test and debug environment variables are documented in
[Build and test](../project/build-test.md) (`KOS_TRACE_*`, `DEBUG`, etc.).
