# Grammar

Koskari source combines **layout blocks** (indentation-based forms such as
`if`, `while`, and `define-syntax`) with **parenthesized** S-expressions. The
reader turns both into list structure; the compiler then expands macros and
dispatches on special forms.

A full formal grammar is not published here yet. For surface syntax you can use
today, see [Layouts](layouts/index.md) and [Special forms](specials/index.md).
