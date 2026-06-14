# koslife

Conway's Game of Life in the terminal, written in Koskari. This package is
the reference example for a **`koskari_scripts`** installed application:
sources live under **`koslife/*.ki`**, with no Python entry shim beyond a
minimal **`__init__.py`** that imports koskari.

## Running

Install from the parent **`examples/life/`** directory (requires koskari):

```bash
uv pip install .
koslife --help
koslife --color
```

During development, install koskari first, then the example package.
Build isolation may need to be disabled when koskari is only available locally.
See the root `Makefile` `venv-koslife` target for hints.

## Layout

| Path | Role |
| ---- | ---- |
| **`cli.ki`** | Argument parser and **`main`** entry |
| **`curse.ki`** | Curses UI loop |
| **`life.ki`** | Board simulation |
| **`__init__.py`** | Ensures the koskari import hook is active |

Packaging metadata is in **`setup.cfg`** (with a minimal **`setup.py`**
shim): **`packages`**, **`koskari_scripts`**, and **`install_requires`**. The
koskari setuptools plugin adds **`*.ki`** / **`*.kio`** **`package_data`** at
build time.

## Options

**`koslife`** accepts **`--wrap`**, and a renderer choice among **`--digits`**,
**`--dots`**, **`--color`**, or **`--default`**. Run **`koslife --help`** for
details.
