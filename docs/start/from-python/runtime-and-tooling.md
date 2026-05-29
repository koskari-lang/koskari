# Runtime and tooling

Python developers already have a workflow: venv, `python script.py`, REPL,
tests. This chapter maps that workflow to Koskari.

---

## Two commands, one ecosystem

> **Q:** What replaces `python`?
>
> **A:** The **`koskari`** command for `.ki` source:
>
> ```bash
> koskari              # REPL
> koskari myprog.ki    # run a script
> koskari -i myprog.ki # run script, then REPL
> ```
>
> For Python itself you still use `python`. Koskari is installed **into** your
> Python environment (`pip install -e ".[prompt_toolkit]"` from
> [Install and run](../install-run.md)).

> **Q:** Is `python -m koskari` the same?
>
> **A:** Yes — same entry point as the `koskari` script. See
> [CLI and configuration](../../reference/cli-and-config.md).

---

## Project layout

> **Q:** Where do `.ki` files live?
>
> **A:** Where Python modules live. Koskari registers an importer so
> `import mypkg.mod` can load `mypkg/mod.ki` when the Koskari importer is
> enabled (default when you `import koskari`). For scripts, any path works:
>
> ```bash
> koskari examples/hello.ki
> ```

> **Q:** Does Koskari tweak `sys.path` like running a script?
>
> **A:** By default, yes — the current directory is added unless you pass
> `--no-tweak-path`.

> **Q:** How do I run tests in this repo?
>
> **A:** Same as other Python projects here: **`tox`** and **`make test`**
> (see [Build and test](../../project/build-test.md)). Koskari’s own tests are
> Python tests that drive the compiler and VM.

---

## REPL habits (Python vs Koskari)

> **Q:** What feels like `python` REPL?
>
> **A:**
>
> ```scheme
> koskari> (+ 1 2)
> 3
> koskari> (define n 10)
> koskari> (* n n)
> 100
> koskari> _
> 100
> ```
>
> Expression results print when they are worth showing. `_` recalls the last
> result — handy when experimenting.

> **Q:** What feels different?
>
> **A:** Syntax errors often mention **mismatched delimiters** because the
> reader is strict. Incomplete forms continue on the next line until
> parentheses balance — similar spirit to Python’s `...` continuation, but
> driven by `(` / `)`.

---

## Compile artifacts

> **Q:** Can I precompile like `.pyc`?
>
> **A:** Yes, to **`.kio`** (Koskari bytecode object):
>
> ```bash
> koskari --compile-kio out.kio myprog.ki
> koskari --execute-kio out.kio
> ```
>
> You still need the Koskari runtime; `.kio` is not a standalone executable.

---

## Tooling extras

> **Q:** Is there an LSP?
>
> **A:** **`koskari-lsp`** — see [CLI and configuration](../../reference/cli-and-config.md). Optional **`prompt_toolkit`** extra gives a highlighted REPL.

> **Q:** Where is Python API documentation?
>
> **A:** [Python API reference](../../api/index.md) — `koskari.importer`,
> `koskari.module`, `koskari.dis`, and related modules for embedding.

---

## What you should remember

Use `koskari` for `.ki`, `python` for `.py`, same venv for both. Scripts,
REPL, path tweaking, and `-i` mirror Python ergonomics. Tests and packaging
stay in the Python toolchain you already use.

Next: [Data and control](data-and-control.md).
