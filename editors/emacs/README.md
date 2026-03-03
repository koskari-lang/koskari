# Koskari Emacs Mode

Major mode for editing Koskari (`.ki`) source files in Emacs. Provides syntax highlighting, indentation for layout blocks, and comment support.

## Installation

### Option 1: Add to load-path and require

Add this to your init file (e.g. `~/.emacs` or `~/.emacs.d/init.el`):

```el
(add-to-list 'load-path "/path/to/sibilant-ng/editors/emacs")
(require 'koskari-mode)
```

Replace `/path/to/sibilant-ng` with the actual path to your sibilant-ng repository.

### Option 2: Load once

From Emacs:

1. `M-x load-file`
2. Choose `editors/emacs/koskari-mode.el` in your sibilant-ng tree

This loads the mode for the current session only.

### Option 3: Byte-compile for faster loading

```sh
cd /path/to/sibilant-ng/editors/emacs
emacs -batch -f batch-byte-compile koskari-mode.el
```

Then add the directory to `load-path` and `require` as in Option 1. Emacs will load the `.elc` when available.

## Usage

- **Auto-mode**: Files with the `.ki` extension use Koskari mode automatically.
- **Shebang**: A first line matching `#!...koskari` also triggers Koskari mode (via `interpreter-mode-alist`).
- **Comments**: Line comments start with `;`. Use `M-;` (`comment-dwim`) to toggle comments.
- **Indentation**: `TAB` indents the current line according to Koskari block structure. `RET` with `electric-indent-mode` (default) reindents new lines.
- **Manual activation**: `M-x koskari-mode` to enable the mode in the current buffer.

## Customization

- `koskari-indent-offset` (default: 4): Number of spaces per indentation level. Customize via `M-x customize-group RET koskari RET`.

## Features

- **Syntax highlighting**: Shebangs, block keywords, function names, keywords (`:foo`, `foo:`), numbers, collection prefixes (`#(`, `#[`, `#{`), and unquote operators
- **Indentation**: Block bodies indent one level after a header ending with `:`; peer blocks (`elif`, `else`, `except`, `finally`) align with their parent `if` or `try`
- **Comment support**: `comment-dwim` and `comment-region` work with `;` line comments
- **S-expression navigation**: Standard `forward-sexp`, `backward-sexp`, etc. work with Koskari structure
