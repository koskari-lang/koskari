# Koskari Helix Configuration

Helix support for Koskari (`.ki`): Tree-sitter syntax highlighting plus
`koskari-lsp` for diagnostics, hover, and completion.

The grammar itself is editor-neutral and lives in
[`../tree-sitter/`](../tree-sitter/). See that directory's README for the full
build-and-install procedure — the **Helix** section there is the canonical
guide. This directory only holds the Helix-specific glue:

- `languages.toml` — language + grammar + `koskari-lsp` configuration. Merge it
  into `~/.config/helix/languages.toml` (or a project `.helix/languages.toml`)
  and set the `[[grammar]]` `source.path` to an absolute path to
  `editors/tree-sitter`.

Quick version:

```sh
pip install -e ".[lsp]"                     # koskari-lsp on PATH
# merge languages.toml, set grammar source path to .../editors/tree-sitter
hx --grammar fetch && hx --grammar build
ln -s /abs/path/to/koskari/editors/tree-sitter/queries \
      ~/.config/helix/runtime/queries/koskari
```

Verify with `hx --health koskari` (Highlight should be enabled) and by opening a
`.ki` file.
