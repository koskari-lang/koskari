# Packaging a Koskari application

Ship a Koskari application as a normal pip-installable command. Users run
`mytool` from their shell; your sources stay in `.ki` files under a declared
Python package.

## Prerequisites

- [Quickstart](../quickstart.md)
- [From Python: Programs and modules](../tutorials/from-python/programs-and-modules.md)
- [Script entry point](../tutorials/from-python/examples/script-main.md) — define a `main` entry callable

## Overview

Koskari uses a **`koskari_scripts`** entry point group (similar in spirit to
Python **`console_scripts`**, but for async Koskari programs). At build time,
the koskari setuptools plugin:

- writes a pre-built stub for each command into the wheel (via
  **`install_scripts`**) that calls **`run_resolved_script(dist==version, name)`**
- adds **`*.ki`** and **`*.kio`** to **`package_data`** for each package you
  list in **`packages=`**
- sets **`zip_safe = False`** so installed sources remain discoverable

At install time, pip copies the stub from the wheel into **`bin/mytool`**. At
runtime the stub resolves the **`koskari_scripts`** target from that package's
dist-info only, then runs it under the koskari async host (no per-app
**`asyncio.run`** shim).

## Minimal packaging

Parent directory layout:

```text
myapp/
  setup.cfg
  setup.py
  pyproject.toml
  mypkg/
    __init__.py
    cli.ki
```

**`setup.cfg`** (metadata and entry points):

```ini
[metadata]
name = myapp
version = 0.1.0
description = My Koskari application

[options]
packages =
    mypkg
install_requires =
    koskari
python_requires = >=3.9

[options.entry_points]
koskari_scripts =
    mytool = mypkg.cli:main
```

**`setup.py`** (minimal shim):

```python
from setuptools import setup

setup()
```

**`pyproject.toml`** (build requires koskari so the plugin runs):

```toml
[build-system]
requires = ["setuptools>=61", "koskari"]
build-backend = "setuptools.build_meta"
```

**`mypkg/cli.ki`** — expose **`main`** with the same contract as Python
**`console_scripts`**: **no parameters**; read CLI args from **`sys.argv`**:

```koskari
(import sys)
(import-from argparse ArgumentParser)

define main []:
  (define-values [prog *: args] sys.argv)
  ;; parse args, run your program, return an integer exit code
  0
```

An optional **`argv`** parameter with a default of **`sys.argv`** is also
fine when **`main`** is called with zero arguments.

## Package **`__init__.py`**

A minimal Python **`__init__.py`** that runs **`import koskari`** is
recommended. It ensures the `.ki` import hook is active when your package
loads, including environments where koskari’s site **`*.pth`** bootstrap is
not installed (common with editable installs).

```python
import koskari
```

You do not need a Python **`main`** function or **`asyncio.run`** wrapper.

## Build and install

From the application directory:

```bash
pip install .
# or: uv pip install .
```

When koskari is only available locally (not on an index), the build environment
must be able to import koskari — for example by installing koskari editable
first, or by disabling build isolation for that build.

After install, verify metadata:

```text
[koskari_scripts]
mytool = mypkg.cli:main
```

The installed **`bin/mytool`** file calls
**`run_resolved_script('myapp==…', 'mytool')`** directly.

Run:

```bash
mytool --help
```

## Rules of thumb

| Topic | Guidance |
| ----- | -------- |
| Entry value | Use **`module:callable`** (for example **`mypkg.cli:main`**) |
| Entry callable | **`main()`** with **no parameters** — same as **`console_scripts`**; read **`sys.argv`** |
| Return value | Integer exit code, **`None`** (treated as 0), or an awaitable producing one |
| **`packages=`** | Declare explicitly; the plugin does not guess package names |
| **`package_data`** | Omit **`*.ki`** / **`*.kio`**; the plugin adds them |
| **`zip_safe`** | Omit; the plugin sets **`False`** when **`koskari_scripts`** is present |
| Name collisions | Do not register the same command name under **`console_scripts`** |
| CI validation | **`from koskari.extras.setuptools import validate_koskari_scripts`** checks entry names and **`module:callable`** values on a **`Distribution`** mock |

## Complete example

The repository includes **`examples/life`**: package **`koslife`**, command
**`koslife`**, entry **`koslife.cli:main`**. See that tree for a working
curses application.

## See also

- [CLI and configuration](../reference/cli-and-config.md)
- [Script entry point](../tutorials/from-python/examples/script-main.md)
- [Demand, async, and defer](demand-async-and-defer.md)

## The bundled icon

Koskari ships its apple mark inside the package, so an application can use
it as a window icon, favicon, or placeholder mark without vendoring the
artwork. `koskari.assets` exposes it as SVG text or bytes, as PNG bytes at
the packaged sizes, or as a filesystem path:

```python
from koskari.assets import (ICON_PNG_SIZES, icon_path, icon_png_bytes,
                            icon_svg_bytes, icon_svg_text)

ICON_PNG_SIZES            # (32, 256), the sizes packaged
icon_svg_text()           # str, the SVG document
icon_svg_bytes()          # bytes, the same document
icon_png_bytes(32)        # bytes, the 32x32 PNG
```

The same functions are reachable from Koskari:

```koskari
import-from koskari.assets icon_png_bytes
define icon_bytes (icon_png_bytes 32)
```

A PNG size that is not packaged raises `ValueError`; check
`ICON_PNG_SIZES` first.

Some APIs want a path rather than bytes: a GUI toolkit's window-icon
setter, a web framework's favicon route. `icon_path` is a context manager
for that. It works when koskari is installed as a zipped wheel, since it
may extract to a temporary file for the duration of the block, so the path
is valid only inside the `with`:

```python
from koskari.assets import icon_path

with icon_path(size=32) as path:
    window.set_icon(str(path))

with icon_path() as svg_path:      # size=None selects the SVG
    favicon_route.serve(svg_path)
```
