# Layout handlers

This document describes the layout system used by the Koskari reader for indent-based block syntax. Layouts are resolved at read time and transform block sources into s-expressions for the compiler.

The `LayoutReader` resolves layout heads from a namespace (`layout/<head>`); handlers live in `koskari/reader/layouts/`. A **`.ki`** module can reuse an existing handler by binding **`layout/<head>`** to another layout name (for example **`define layout/foo layout/flat`** in **`basics.ki`**) instead of adding Python code.


## Blocks

A **block** is the basic unit of layout syntax. It consists of:

1. **Header:** forms parsed until a block opener (colon) is seen. The first form
   is the **head**; remaining forms are **args**.
2. **Body:** indented forms following the header, read until dedent or EOF.

**Block opener:** A standalone colon `:` or a keyword ending with `:` (e.g.
`else:`). The parser treats `x:` and `x :` identically.

**Top-level detection:** At top level (not inside `()`, `[]`, or `{}`), the reader treats the input as a block: it reads a header until a colon, then reads the indented body. Inside delimiters, layout mode is disabled and the base s-expression reader is used.

**Resolution:** The reader looks up `layout/<head>` in the namespace. If found and it is a `LayoutHandler`, that handler's `read(source, peers)` is invoked. If not found, the block is treated as a default block and routed through `layout/block`.

## `:block:` cons data and `apply_layout`

Read-time layout handlers turn indented blocks into ordinary list forms. Some compiler paths preserve **`(:block: …)`** cons structure as **data** — notably **`compile/quote-block`**, filled **`compile/quasiquote`** / **`compile/quasiquote-block`** results, and macro templates that intentionally retain blocks.

**`koskari.reader.layouts`** exports **`find_layout`**, **`apply_layout`** (one primary block), and **`apply_layouts`** (walk a proper spine, consuming declared peers). **`LayoutReader.find_layout`** delegates to the same **`find_layout`** helper. **`koskari.syntax._finalize_macro_output`** calls **`apply_layout`** on macro and **`syntax-rules`** output before hygiene discovery so expansion matches reader layout behaviour.

**Flat blocks (`layout/flat`) and Ki aliases:** The handler **`layout/flat`**
splices the indented body into the header list: **`(HEAD ARG … FORM …)`** with no extra wrapper around the body forms. Many block heads only need that shape. In **`koskari/importer/basics.ki`**, **`define layout/define-syntax layout/flat`** and **`define layout/extend-syntax layout/flat`** attach the same handler to
those heads so **`define-syntax`** and **`extend-syntax`** blocks read as flat **`(define-syntax NAME TRANSFORMER)`** and **`(extend-syntax NAME TRANSFORMER)`** lists. That avoids a dedicated Python layout for those forms and avoids wrapping the transformer in **`compile/begin`** at read time. For any new head with the same “call-like header + body as tail arguments” shape, you can **`define
layout/<head> layout/flat`** in **`.ki`** instead of implementing a handler in Python (see **Adding New Layouts** below).

```{toctree}
:maxdepth: 1
:glob:

layout-*
```
