# Using the default app icon

Koskari ships its own apple-mark logo inside the package, so any
koskari-written program can use it as a default window icon, favicon, or
placeholder brand mark without depending on the docs tree or vendoring a
copy of the artwork.

## Prerequisites

- [Install and run](../../start/install-run.md)
- [From Python: Interop](../../start/from-python/interop.md)

## Getting the icon

`koskari.assets` exposes the mark as SVG text/bytes, as PNG bytes at a
couple of pre-rendered sizes, or as a real filesystem path:

```python
from koskari.assets import (ICON_PNG_SIZES, icon_path, icon_png_bytes,
                            icon_svg_bytes, icon_svg_text)

ICON_PNG_SIZES            # (32, 256) -- the sizes actually packaged

icon_svg_text()           # str, the SVG document
icon_svg_bytes()          # bytes, same document
icon_png_bytes(32)        # bytes, the 32x32 PNG rendering
```

The same functions are reachable from Koskari itself, since `.ki` imports
any Python package on `sys.path`:

```
import-from koskari.assets icon_png_bytes
define icon_bytes (icon_png_bytes 32)
```

Requesting a PNG size that isn't packaged raises `ValueError` — check
`ICON_PNG_SIZES` rather than guessing.

## When you need a real path

Some APIs (a GUI toolkit's window-icon setter, a web framework's favicon
route) want a filesystem path, not bytes. `icon_path` is a context
manager for exactly that; it works even if koskari is installed as a
zipped wheel, since it may need to extract to a temporary file for the
duration of the block:

```python
from koskari.assets import icon_path

with icon_path(size=32) as path:
    window.set_icon(str(path))     # path is only valid inside the block

with icon_path() as svg_path:      # size=None -> the SVG
    favicon_route.serve(svg_path)
```

Don't hold on to the path after the `with` block exits.

## See also

- [Hosting Koskari in a Python app](embedding-host.md) — the broader
  Python/Koskari interop story
