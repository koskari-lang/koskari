# Lexer Subpackage

This document designs the `koskari.lexer` subpackage, which provides lexical and structural analysis of Koskari source without compilation or evaluation. It supports syntax highlighting, completion, and Language Server Protocol (LSP) features for editor integration.

## Goals

- Provide a **lexical-only parser** that reads source into s-expressions or tokens with positions, without compile or eval.
- Support **prompt_toolkit** (REPL syntax highlighting, completion) via an optional submodule.
- Support **LSP** (diagnostics, hover, go-to-definition, completion) via an optional submodule.
- Keep optional integrations as extras; core koskari remains dependency-light.

## Relationship to Existing Reader

The existing `koskari.reader` already parses source into s-expressions. The lexer subpackage does not replace it; it provides:

1. A **factory** for an abstract reader backed by default atoms, reader macros, and layouts (no live module or execution context).
2. **Token-level output** for consumers that need a token stream (e.g. syntax highlighters) rather than s-expressions.
3. **Name sets** (specials, layouts) for highlighting and completion, without invoking the compiler.

The abstract reader reuses `LayoutReader` with a static namespace (LAYOUTS or `make_builtins()`). Token emission may be implemented by walking parsed s-expressions or by a separate tokenizer.

## Package Structure

```
koskari/
├── lexer/
│   ├── __init__.py
│   ├── parser.py          # default_layout_reader, parse helpers
│   ├── tokens.py          # Token type, tokenize(), name sets
│   ├── prompt_toolkit/    # optional: koskari[prompt_toolkit]
│   │   ├── __init__.py
│   │   ├── lexer.py       # prompt_toolkit Lexer for syntax highlighting
│   │   └── completer.py   # (optional) REPL completer
│   └── lsp/               # optional: koskari[lsp]
│       ├── __init__.py
│       ├── server.py      # LSP server entry point
│       └── handlers.py    # diagnostics, hover, completion, etc.
```

## Core Module: koskari.lexer

### parser.py

**`default_layout_reader()`**

Returns a `LayoutReader` instance configured with default atoms, reader macros, and layouts. No module, no execution. Uses a static namespace built from `koskari.reader.layouts.LAYOUTS` (or `koskari.importer.builtins.make_builtins()` if layout set should match runtime exactly).

```python
def default_layout_reader():
    """Reader with default atoms, macros, and layouts. Read-only; no execution."""
    from koskari.reader.layouts import LAYOUTS
    from koskari.reader import LayoutReader
    ns = {str(k): v for k, v in LAYOUTS.items()}
    return LayoutReader(ns)
```

**`parse(source, filename='<string>')`**

Convenience: `source_str` + `default_layout_reader().read()`. Returns s-expression or None at EOF. For multiple top-level forms, call in a loop.

### tokens.py

**`Token`**

Named tuple or dataclass: `(type: str, text: str, start_line, start_col, end_line, end_col)`. Token types: `'symbol'`, `'keyword'`, `'string'`, `'number'`, `'comment'`, `'paren'`, `'special'`, `'layout'`, etc.

**`tokenize(source, filename='<string>')`**

Yields `Token` instances. Implementation options:

- **Parse-and-walk**: Use `default_layout_reader()` to parse, then walk the s-expr tree. Each atom yields a token (type from atom kind; position from `get_position()`). Delimiters inferred from pair positions.
- **Dedicated tokenizer**: Standalone character scanner that mirrors reader token boundaries. More complex but does not require a full parse; useful for partial/invalid input.

Initial implementation may use parse-and-walk. A dedicated tokenizer can be added later for robustness on malformed input.

**Name sets (for highlighting and completion)**

- **`SPECIAL_NAMES`**: `frozenset(str(s) for s in koskari.compile.specials.SPECIALS)`
- **`LAYOUT_NAMES`**: `frozenset(str(s) for s in koskari.reader.layouts.LAYOUTS)` (layout heads like `layout/if`, `layout/block`)

These provide names only; no expansion or execution.

## Optional Submodule: koskari.lexer.prompt_toolkit

**Extras**: `koskari[prompt_toolkit]` adds `prompt_toolkit` as a dependency.

### lexer.py

**`KoskariLexer`**

Implements prompt_toolkit’s `Lexer` interface:

```python
def lex_document(self, document: Document) -> Callable[[int], Iterable[Tuple[Token, str]]]:
```

Uses `tokenize()` (or parse-and-walk) to yield `(prompt_toolkit.Token, text)` per line. Token type strings (e.g. `'Token.Keyword'`, `'Token.String'`) map to prompt_toolkit `Style` for coloring.

**`koskari_style`**

A prompt_toolkit `Style` instance or style-from-dict for Koskari token types. May be composed with `merge_styles()`.

### completer.py (optional)

**`KoskariCompleter`**

prompt_toolkit `Completer` that suggests:

- Special form names from `SPECIAL_NAMES`
- Layout names from `LAYOUT_NAMES`
- (Future) symbols from a read-only namespace snapshot for REPL completion

Can be passed to `PromptSession` as `completer=...`.

## Optional Submodule: koskari.lexer.lsp

**Extras**: `koskari[lsp]` adds `pygls` as a dependency.

### LSP Library Choice

**Recommendation: use pygls.**

- Pythonic LSP implementation, widely used (1M+ monthly downloads).
- Handles JSON-RPC, document sync, and protocol details.
- Supports sync and async; STDIO, TCP, WebSocket.
- Lets us focus on language-specific logic (parsing, diagnostics, hover, completion).

Implementing from scratch is possible but reinvents protocol handling. LSP is stable; pygls is maintained and sufficient for our needs.

### server.py

**`create_server()`**

Returns a `pygls` LanguageServer instance with handlers registered. Used as:

```python
server = create_server()
server.start_io()  # STDIO
```

**`main()`**

Entry point for `koskari-lsp` console script when extras are installed.

### handlers.py

**Diagnostics** (`textDocument/didOpen`, `textDocument/didChange`)

- Parse with `default_layout_reader()`.
- On `ReaderSyntaxError`, emit diagnostic with range and message.
- Clear diagnostics on successful parse.

**Hover** (`textDocument/hover`)

- Parse and locate form at position (walk s-expr tree by position).
- If head is a special or layout, return docstring/signature if available.

**Completion** (`textDocument/completion`)

- Use position context (e.g. top-level, inside form) and `SPECIAL_NAMES`, `LAYOUT_NAMES`.
- Filter by prefix. Return `CompletionItem` list.

**Go to definition** (optional)

- Requires symbol resolution; may need compile-time analysis or limited evaluation. Defer to a later phase.

### LSP capabilities (initial)

- `textDocumentSync`: incremental
- `textDocument/publishDiagnostics`
- `textDocument/hover`
- `textDocument/completion`

Others (signature help, document symbols, etc.) can be added incrementally.

## Optional Dependencies

```ini
[options.extras_require]
prompt_toolkit = ["prompt_toolkit>=3.0"]
lsp = ["pygls>=2.0"]
all = ["koskari[prompt_toolkit,lsp]"]
```

Imports in optional submodules should be lazy or guarded so that `koskari.lexer` core works without them:

```python
# koskari/lexer/prompt_toolkit/__init__.py
def __getattr__(name):
    if name == "KoskariLexer":
        from .lexer import KoskariLexer
        return KoskariLexer
    raise AttributeError(name)
```

Or document that `koskari.lexer.prompt_toolkit` / `koskari.lexer.lsp` require their extras and will raise `ImportError` if dependencies are missing.

## Test Layout

- **tests/lexer/**: `test_parser.py`, `test_tokens.py`
- **tests/lexer/prompt_toolkit/**: `test_lexer.py` (optional; skip if prompt_toolkit not installed)
- **tests/lexer/lsp/**: `test_handlers.py` (optional; skip if pygls not installed)

Core lexer tests run in the default tox env. Optional integration tests use `pytest.importorskip("prompt_toolkit")` or `pytest.importorskip("pygls")`.

## Implementation Order

1. **Core**: `parser.py` (default_layout_reader, parse), `tokens.py` (Token, tokenize, name sets).
2. **prompt_toolkit**: KoskariLexer, style. Validate in REPL.
3. **LSP**: pygls server, diagnostics, hover, completion. Validate with a client (e.g. VS Code, Cursor).

## Summary

The lexer subpackage centralizes read-only parsing and token emission for editor tooling. It reuses the existing Reader and LayoutReader with a static default namespace. Optional submodules integrate with prompt_toolkit (highlighting, completion) and LSP (diagnostics, hover, completion) via pygls. Both integrations are extras; the core remains dependency-light.
