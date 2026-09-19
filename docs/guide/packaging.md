# Packaging a Koskari application

Ship a Koskari application as a pip-installable command. Users run
`mytool` from a shell; the sources stay in `.ki` files under a declared
Python package.

## How it works

Koskari defines a `koskari_scripts` entry point group, the counterpart
of `console_scripts` for Koskari programs. At build time the Koskari
setuptools plugin:

- writes a stub for each command into the wheel, through
  `install_scripts`, that calls `run_resolved_script(dist==version, name)`
- adds `*.ki` and `*.kio` to `package_data` for each package listed in
  `packages=`
- sets `zip_safe = False` so the installed sources stay discoverable

At install time pip copies the stub into `bin/mytool`. At run time the
stub resolves the `koskari_scripts` target from that package's
dist-info and runs it under the Koskari async host, so the application
needs no `asyncio.run` of its own.

## A minimal package

```text
myapp/
  setup.cfg
  setup.py
  pyproject.toml
  mypkg/
    __init__.py
    cli.ki
```

`setup.cfg` carries the metadata and the entry point:

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

`setup.py` is the minimal shim:

```python
from setuptools import setup

setup()
```

`pyproject.toml` lists koskari as a build requirement so the plugin
runs:

```toml
[build-system]
requires = ["setuptools>=61", "koskari"]
build-backend = "setuptools.build_meta"
```

`mypkg/cli.ki` exposes `main` with the `console_scripts` contract: no
parameters, arguments read from `sys.argv`, an integer exit code
returned:

```koskari
(import sys)
(import-from argparse ArgumentParser)

define main []:
  (define-values [prog *: args] sys.argv)
  ;; parse args, run the program, return an exit code
  0
```

An `argv` parameter with a default of `sys.argv` is also accepted, since
`main` is called with no arguments.

`mypkg/__init__.py` runs `import koskari`, which turns the `.ki` import
hook on when the package loads, including in environments where
Koskari's site `.pth` bootstrap is absent, as it is in some editable
installs:

```python
import koskari
```

No Python `main` function and no `asyncio.run` wrapper is needed.

## Build and install

From the application directory:

```bash
pip install .
```

When koskari is available only locally, the build environment must be
able to import it: install koskari editable first, or disable build
isolation for that build.

After the install, the distribution's entry points list the command:

```text
[koskari_scripts]
mytool = mypkg.cli:main
```

The installed `bin/mytool` calls `run_resolved_script('myapp==...',
'mytool')`, and `mytool --help` runs it.

## Rules

| Topic | Rule |
| ----- | ---- |
| entry value | `module:callable`, for example `mypkg.cli:main` |
| entry callable | `main()` with no parameters; read `sys.argv` |
| return value | an integer exit code, `None` for 0, or an awaitable producing one |
| `packages=` | declare it; the plugin does not guess package names |
| `package_data` | omit `*.ki` and `*.kio`; the plugin adds them |
| `zip_safe` | omit it; the plugin sets `False` when `koskari_scripts` is present |
| name collisions | do not register the same command under `console_scripts` |
| CI validation | `koskari.extras.setuptools.validate_koskari_scripts` checks entry names and `module:callable` values on a `Distribution` |

The repository's `examples/life` is a complete application of this
shape: package `koslife`, command `koslife`, entry `koslife.cli:main`, a
curses program.

## The bundled icon

Koskari ships its apple mark inside the package, so an application can
use it as a window icon, favicon, or placeholder mark without vendoring
the artwork. `koskari.assets` exposes it as SVG text or bytes, as PNG
bytes at the packaged sizes, or as a filesystem path:

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

A PNG size that is not packaged raises `ValueError`; `ICON_PNG_SIZES`
lists the packaged ones.

Some APIs want a path rather than bytes: a GUI toolkit's window-icon
setter, a web framework's favicon route. `icon_path` is a context
manager for that. It works when koskari is installed as a zipped wheel,
since it may extract to a temporary file for the duration of the block,
so the path is valid only inside the `with`:

```python
from koskari.assets import icon_path

with icon_path(size=32) as path:
    window.set_icon(str(path))

with icon_path() as svg_path:      # size=None selects the SVG
    favicon_route.serve(svg_path)
```

## See also

- [CLI and configuration](../reference/cli-and-config.md)
- [Script entry point](../tutorials/from-python/examples/script-main.md)
- [Hosting Koskari from Python](hosting-from-python.md)
