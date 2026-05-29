# Glossary

Shared terms used across tutorials and reference pages.

**form**
: A Koskari expression: atoms, lists, or layout-derived s-expressions.

**special form**
: A form whose head is a compiler symbol such as `compile/call` or `compile/lambda`.
  Specials are implemented in Python under `koskari.compile.specials`.

**macro**
: A compile-time transformer defined with `define-syntax`. Expands to ordinary
  forms before compilation.

**layout**
: A read-time handler for indent-based block syntax (`layout/if`, `layout/block`, ...).

**namespace**
: Lexical environment mapping names to reference cells.

**pair**
: A cons cell (`car` / `cdr`); lists are proper chains of pairs terminated by `nil`.

**bytecode**
: `KosCode` instruction sequence executed by the Koskari VM.

**opcode**
: One VM instruction (see [Opcode reference](vm/opcodes/index.md)).

**REPL**
: Read-eval-print loop started by `koskari` with no script argument.
