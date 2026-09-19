# Changelog

User-visible changes: language behavior, the command line, tooling, and
documentation. Each release entry says what changed for a reader first
and links to the page that covers it.

## 0.1.0

The first public release.

- S-expressions and an indentation layout that reads to the same forms:
  a line with more than one element is a call, and a line ending in a
  colon opens a block.
- A stackless bytecode evaluator with tail calls, generators, delimited
  and full continuations, dynamic wind, and ambient await.
- Special forms, bootstrap macros, `define-syntax` with template and
  procedural transformers, and hygiene.
- Python interoperability in both directions: `.ki` modules import
  through the standard import system, and every value is a Python
  object.
- Runtime `match` patterns, by-reference parameters, decorators with
  graft and call-time forms, fenced foreign literals, and module
  pragmas.
- The `koskari` command, `.kio` compilation, a language server, and
  editor support for Emacs, Helix, and TextMate.
- Documentation: a quickstart, the Little Koskari and From Python
  tutorials, the guide, and the reference.
