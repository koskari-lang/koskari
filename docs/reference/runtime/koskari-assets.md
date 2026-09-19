# `koskari.assets`

The brand artwork packaged with the wheel, so a program can use it as a window
icon, favicon, or placeholder mark without vendoring the files. The module
holds the apple mark as an SVG and two pre-rendered PNG sizes.

| Name | Does |
|------|------|
| `ICON_PNG_SIZES` | The packaged PNG sizes, `#(32 256)` |
| `(icon_svg_text)` | The SVG document as text |
| `(icon_svg_bytes)` | The same document as bytes |
| `(icon_png_bytes size: 32)` | The PNG at one of the packaged sizes |
| `(icon_path size: None)` | A context manager yielding a filesystem path |

## Bytes

```koskari
>>> import-from koskari.assets ICON_PNG_SIZES icon_png_bytes icon_svg_text
>>> ICON_PNG_SIZES
#(32 256)
>>> (len (icon_png_bytes 32))
1889
>>> (len (icon_svg_text))
3151
```

A size that is not packaged raises `ValueError`, naming the sizes that are:

```koskari
>>> (icon_png_bytes 64)
ValueError: no packaged icon at size 64, have (32, 256)
```

## A path instead of bytes

Some APIs want a path -- a GUI toolkit's window-icon setter, a web framework's
favicon route. `icon_path` is a context manager for that. The package may be
installed as a zipped wheel, in which case the file is extracted to a temporary
path for the duration of the block, so the path is valid only inside the
`with`.

`size:` selects a PNG; leaving it out selects the SVG.

```koskari
>>> import-from koskari.assets icon_path
>>> with [p (icon_path size: 32)]:
...   p.name
"koskari-icon-32.png"
>>> with [p (icon_path)]:
...   p.name
"koskari-icon.svg"
```

## From Python

The same names import from Python, where the functions are ordinary callables:

```python
from koskari.assets import (ICON_PNG_SIZES, icon_path, icon_png_bytes,
                            icon_svg_bytes, icon_svg_text)

ICON_PNG_SIZES            # (32, 256)
icon_svg_text()           # str, the SVG document
icon_svg_bytes()          # bytes, the same document
icon_png_bytes(32)        # bytes, the 32x32 PNG

with icon_path(size=32) as path:
    window.set_icon(str(path))

with icon_path() as svg_path:      # size=None selects the SVG
    favicon_route.serve(svg_path)
```
