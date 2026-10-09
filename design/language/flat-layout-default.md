# Default block layout (flat splice)

Agent reference for the default unknown-head block behaviour. User-facing
normative syntax: [docs/reference/reader-layout.md](../../docs/reference/reader-layout.md).

## Default handler

**`layout/block`** (`koskari/reader/layouts/basics.layout_block`) is the
fallback when **`apply_layout`** / **`apply_layouts`** finds no
**`layout/<head>`** for a block's head symbol.

Read result: **`(HEAD ARG … BODY-FORM …)`** — body forms are spliced after
header args with no **`compile/begin`** wrapper.

## Opt-in begin wrap

**`layout/block-begin`** produces **`(HEAD ARG … (compile/begin BODY …))`**.
Bind in **`.ki`**: **`define layout/<head> layout/block-begin`**.

## Read pipeline

1. **Parse** — colon blocks → **`:#block:`** cons holding the header and one
   entry per body line, a **`:#line:`** cons of elements or a nested block; stream peers →
   spine of **`:#block:`** conses for **`apply_layouts`**
2. **Expand** — **`LayoutReader.read()`** → **`apply_layout`** /
   **`apply_layouts`** → handler **`read(reader, source, peers)`**, which
   folds lines through the reader

## Heads that rely on the default

Symbol heads with no dedicated **`layout/<head>`** registration use flat splice,
including **`begin`**, **`define-syntax`**, **`extend-syntax`**, and unknown
call-like blocks.

Named layouts (**`layout/if`**, **`layout/try`**, **`layout/switch`**, …) have
dedicated handlers. Inner children (for example **`case`** / **`else`** under
**`switch`**) are often expanded by **`layout/block`** before the parent handler
rewrites them.

## References

- [LAYOUTS.md](../LAYOUTS.md) — handler rollup
- [docs/reference/layouts/layout-block-default.md](../../docs/reference/layouts/layout-block-default.md)
- [docs/reference/layouts/layout-block-begin.md](../../docs/reference/layouts/layout-block-begin.md)
- **`koskari/reader/layouts/basics.py`**
