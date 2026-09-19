# Release and versioning

Koskari 0.1.0 is the first public release, a beta. Versions follow
`major.minor.patch`. Through the `0.x` series the language surface may
change between minor versions, and the [changelog](../changelog.md)
records each change for readers before anything else.

## What may change

- Macro and layout conveniences: a form may gain a shape or lose one
  that nothing used.
- Error wording and diagnostic detail.
- Tooling: highlighting, the language server, the Sphinx extension.
- Bytecode and evaluator internals, which change faster than the
  language.

## What holds

- Tutorial and guide examples run as shown for the version series that
  documents them.
- Reference pages describe the current implementation; a form's page
  changes when the form does.
- A `.kio` file is tied to the bytecode version that produced it, and a
  mismatch is refused rather than misread.

Pin the package version in a project the way any Python dependency is
pinned.

## Reporting drift

An example that does not run as shown is a bug. Report it with the
Koskari version, the exact command, the full output, and the page and
section.
