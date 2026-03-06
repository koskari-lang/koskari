# Koskari Helix Configuration

Helix configuration for Koskari (`.ki`) files: tree-sitter syntax highlighting and `koskari-lsp` for diagnostics, hover, and completion.

## Prerequisites

1. **Koskari with LSP**: `pip install -e ".[lsp]"` so `koskari-lsp` is on your PATH.
2. **Tree-sitter grammar**: Build the grammar (see below).
3. **Node.js** (for `tree-sitter` CLI): `npm install -g tree-sitter`.

## Grammar Build

From the sibilant-ng repo root:

```sh
cd editors/helix/tree-sitter-koskari
npm install
npx tree-sitter generate
```

Then tell Helix about the grammar. In your `languages.toml`, set the grammar source path to the absolute path of `tree-sitter-koskari`:

```toml
[[grammar]]
name = "koskari"
source = { path = "/abs/path/to/sibilant-ng/editors/helix/tree-sitter-koskari" }
```

Then:

```sh
hx --grammar fetch
hx --grammar build
```

Symlink the queries so Helix can find them:

```sh
ln -s /abs/path/to/sibilant-ng/editors/helix/tree-sitter-koskari/queries \
      ~/.config/helix/runtime/queries/koskari
```

For project-local config, use the appropriate runtime path (e.g. `$HELIX_RUNTIME`).

## Installation

Merge the contents of `languages.toml` into your Helix config.

### User config

Append or merge into `~/.config/helix/languages.toml` (or `$XDG_CONFIG_HOME/helix/languages.toml`). Ensure the grammar `source.path` is absolute.

### Project config

Copy `languages.toml` into `.helix/` at your project root and adjust the grammar path if needed.

## Features

- **Syntax highlighting**: Tree-sitter grammar for s-expressions, layout blocks, strings, numbers, keywords, and special forms
- **Diagnostics**: Parse and layout errors from koskari-lsp
- **Hover**: Documentation for special forms and layout heads
- **Completion**: Tab completion for special forms and layout heads

## Verification

1. `hx --health languages` — `koskari` should show Highlight and Textobject as enabled when the grammar and queries are installed.
2. Open a `.ki` file — syntax coloring should appear.
3. Introduce a syntax error — red squiggles from the LSP.
4. Hover over `define` or `if` — hover text.
5. Trigger completion — special forms and layout heads.
