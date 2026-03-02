# Koskari TextMate Grammar

TextMate syntax highlighting for Koskari (`.ki`) source files. Use with VSCode or any editor that supports TextMate grammars.

## Installation (VSCode)

### Option 1: Install from folder

1. Open VSCode and run **Developer: Install Extension from Location** from the Command Palette (Ctrl+Shift+P / Cmd+Shift+P).
2. Select this directory (`editors/textmate/`).
3. The extension will be installed and `.ki` files will use Koskari syntax highlighting.

### Option 2: Build and install from VSIX

1. Install the VSCode Extension Manager: `npm install -g @vscode/vsce`
2. From this directory, run: `vsce package`
3. Install the generated `.vsix` via **Extensions: Install from VSIX** or: `code --install-extension koskari-0.1.0.vsix`

## Usage

- Files with the `.ki` extension are associated with Koskari automatically.
- Shebang detection: `#!...koskari` on the first line also triggers Koskari mode.
- Comment toggle: `;` for line comments (via language configuration).

## Grammar scope

The grammar covers:

- Comments and shebangs
- Strings (double, triple, raw, format with interpolation)
- Numbers (integer, hex, octal, binary, float, decimal, fraction, complex)
- Lists `()`, `[]`, `{}` and collections `#()`, `#[]`, `#{}`
- Quote and quasiquote forms (`'` and `` ` ``)
- Unquote operators (`,` and `,@`)
- Layout block headers (`if`, `elif`, `else`, `try`, `except`, `finally`, `begin`, `define`, `when`, etc.)
- Keywords (`:foo` and `foo:` inside delimiters)
- Symbols
