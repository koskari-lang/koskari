# Runtime and tooling

Python developers already have a workflow: venv, `python script.py`, REPL,
tests. This chapter maps that workflow to Koskari for **writing and running
`.ki` programs**.

---

## What you need day to day

Two commands share one virtual environment: **`python`** for `.py`, **`koskari`**
for `.ki`. Install Koskari into the same venv you use for Python libraries
(see [Install and run](../install-run.md)).

---

## Running Koskari

> **Q:** What replaces `python myscript.py`?
>
> **A:**
>
> ```bash
> koskari myprog.ki
> koskari -i myprog.ki    # run script, then REPL
> koskari                  # REPL only
> ```
>
> `python -m koskari` is the same entry point. See
> [CLI and configuration](../../reference/cli-and-config.md).

> **Q:** First program to open?
>
> **A:** From the repository root after install:
>
> ```bash
> koskari examples/hello.ki
> ```
>
> It defines a small class, constructs an instance, and prints attribute values.

---

## Project layout

> **Q:** Where do `.ki` files live?
>
> **A:** Alongside Python modules. With the Koskari importer enabled (default
> when you `import koskari`), `import mypkg.mod` can load `mypkg/mod.ki`.

> **Q:** Does Koskari tweak `sys.path`?
>
> **A:** By default the current directory is added when running a script, unless
> you pass `--no-tweak-path`.

---

## REPL habits

> **Q:** What feels like the Python REPL?
>
> **A:**
>
> ```koskari
> koskari> (+ 1 2)
> 3
> koskari> define n 10
> koskari> (* n n)
> 100
> koskari> _
> 100
> ```
>
> Useful results print; `_` recalls the last result. You can define functions with
> layout blocks at the REPL when the reader is waiting for the full indented body.

> **Q:** What feels different?
>
> **A:** Syntax errors often mention **mismatched delimiters**. Incomplete forms
> continue on the next line until parentheses balance.

---

## Compile artifacts

> **Q:** Precompile like `.pyc`?
>
> **A:**
>
> ```bash
> koskari --compile-kio out.kio myprog.ki
> koskari --execute-kio out.kio
> ```
>
> You still need the Koskari runtime; `.kio` is not a standalone executable.

---

## Tooling extras

> **Q:** Highlighted REPL?
>
> **A:** Install with the `prompt_toolkit` extra for syntax-colored input.

> **Q:** Editor support?
>
> **A:** TextMate, Emacs, and tree-sitter grammars ship with Koskari under
> `editors/`. **`koskari-lsp`** is documented in
> [CLI and configuration](../../reference/cli-and-config.md).

> **Q:** Embedding Koskari in a Python application?
>
> **A:** See [Interop](interop.md) and the [Python API reference](../../api/index.md)
> — not required for everyday `.ki` scripting.

---

## Maintainers only

Contributors to the Koskari project itself run the Python test suite via
**`tox`** and **`make test`** (see project docs). That workflow is separate from
learning to write `.ki` programs.

---

## Try it

```bash
koskari examples/hello.ki
```

You should see a greeting and output from a small class. Open the file and
match each `print` to the source.

---

## Remember

Use `koskari` for `.ki`, `python` for `.py`, same venv for both. Use the REPL
for quick experiments; reach for API docs only when embedding.

Next: [Data and control](data-and-control.md).
