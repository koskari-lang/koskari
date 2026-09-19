# Layout handler reference

Per-head read-time lowering for indent-based block syntax. General layout
**syntax** (blocks, lines, colon rules, peers) lives in
[Reader and layout syntax](../reader-layout.md). Motivation and the layout /
s-expression relationship are in
[Koskari and s-expressions](../../guide/layout-and-s-expressions.md).

The `LayoutReader` resolves layout heads from a namespace (`layout/<head>`);
handlers live in `koskari/reader/layouts/`. A **`.ki`** module can reuse an
existing handler by binding **`layout/<head>`** to another layout name (for
example **`define layout/my-head layout/block-begin`**) instead of adding Python
code.

Block syntax rules are in [Reader and layout syntax](../reader-layout.md).

## `:block:` conses and layout expansion

Indented blocks are first read as **`:block:`** conses, the header's elements
as one list and then one entry per body line, each a line of elements or a
nested block:

```
(:block: (HEAD ARG ...) ENTRY ...)
ENTRY ::= (ELEMENT ...) | (:block: ...)
```

**`LayoutReader.read()`** expands those conses through
**`koskari.reader.layouts`**: **`apply_layout`** for a single block,
**`apply_layouts`** for a primary block plus stream-collected peer blocks on one
spine. Each call looks up **`layout/<head>`** in the module namespace (then
builtins) and invokes the handler as **`read(reader, source, peers)`**, where
**`source`** is the cons's cdr, the header and the block's entries, unfolded;
the handler calls **`reader.fold_line`** or **`reader.fold_lines`** where it
wants forms.

Unknown symbol heads use the default **`layout/block`** handler (flat splice).
See [layout/block](layout-block-default.md). Opt-in **`compile/begin`** wrapping
uses [layout/block-begin](layout-block-begin.md).

Some paths preserve **`:block:`** structure as data instead of expanding it —
quoted and quasiquoted blocks (via **`layout/quote`** and
**`layout/quasiquote`**), and macro templates that retain blocks. Macro and
**`syntax-rules`** output is finalized through
**`koskari.syntax._finalize_macro_output`**, which calls **`apply_layout`**
before hygiene discovery so expansion matches reader layout behaviour.

## Adding new layouts

1. When a block only needs flat splicing (header forms followed by body forms in
   one list), no dedicated handler is required — the default **`layout/block`**
   applies when no **`layout/<head>`** is registered.
2. When the body must be wrapped in **`compile/begin`**, add
   **`define layout/<head> layout/block-begin`** in **`.ki`**, or implement a
   custom handler.
3. Otherwise implement a handler with signature **`(reader, source, peers)`**
   in the appropriate module under **`koskari/reader/layouts/`**, decorate with
   **`@layout(symbol('layout/head'), peers=(...))`** when peer blocks are
   needed, and register the handler in the layout namespace (builtins or module
   globals).
4. Add a handler page under **`docs/reference/layouts/`** documenting syntax,
   read shape, and peers.

```{toctree}
:maxdepth: 1
:glob:

layout-*
```
