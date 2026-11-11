# Koskari Design: raw forms in templates, `datumq` and `-'`

## Status

Archived specification. Implemented on branch `hygiene-rework`,
2026-09-24: the template directive in `1963d49f` with its docs and
corpus records in `953ae47f`, the `-'` reader shorthand in `b5eeba3b`
with its docs, editor grammars, and corpus records in `c6b31215`, and
the per-level layout registration in `1715810f`. The scheduler example
writes its shared slots with it in `9a5c99c8`. The rollups are
[MATCHER.md](../MATCHER.md) and [MACROS.md](../MACROS.md); the user
pages are `docs/guide/hygiene.md`, `docs/reference/grammar.md`, and
`docs/reference/macros/compile-datumq.md`.

As built, against the text below:

- The directive applies wherever a template is transcribed, which is
  the one `SyntaxTransform` path that `syntax-rules` and `syntax-match`
  share. Neither has guards, and a `match` clause body is compiled code,
  not a template.
- The layout reader registers `-'`, `&`, and the unary prefixes once per
  level, each bound to its own reader: at the layout level a header
  operand keeps its block colon, and inside parens and backticks the
  inner reader's operand read sees the unquote a backtick installs.
- The lexer highlights `-'` with the quote class.
- The `-` predicate stays until
  [reader-macro-triggers.md](../reader-macro-triggers.md) is implemented.

## Problem

A `syntax-rules` or `syntax-match` template marks every symbol it
writes, so a name it binds is private to its expansion. Macros that
cooperate through a shared module-level name cannot write that name in
a template. The scheduler example's variety macros shared
`default/<type>` that way; after the rework the procedural dispatchers
`job` and `jobset` pass the slot in as a use-site name, since only a
procedural arm can emit one, through `(datum ...)`.

A procedural arm already has the escape: `(datum x)` and `(datumq x)`
wrap a form in a `Datum`, which hygiene treats as the caller's AST and
does not mark. A template has none.

## Phase 1: `datumq` in a template body

The transcriber (`expand_template_with_hygiene`) treats a template pair
whose head is the raw `datumq` symbol as a construction directive:
`(datumq X)` emits `Datum(X)` with `X` taken raw, not marked and not
entered, so a pattern variable inside it is not substituted. `datum` in
a template is an ordinary form, transcribed like any other call, since
a template has no value to evaluate.

The finalize step peels the `Datum`, as it does a procedural arm's
output. In a procedural arm `datumq` stays the existing bootstrap
macro, which yields the same `Datum` at run time; inside a backtick the
escape is `,(datumq foo)`, and a bare `(datumq foo)` there is quoted
data like any other form.

### Nesting and literal forms

A transcription consumes each directive in its template. The head is
matched by identity against the raw symbol: a `datumq` an outer
template wrote was consumed at that level, so an inner template sees
only raw ones.

- A template that must emit a literal `(datumq ...)` call, as a
  template that defines a procedural macro may, writes
  `((datumq datumq) ...)`: the head comes out as the raw symbol and the
  form is not a directive at this level. A literal `(datum ...)` call
  needs no escape.
- A macro-generating template writes `(datumq (datumq foo))`. The outer
  transcription emits the raw `(datumq foo)` into the inner template,
  and the inner transcription consumes it. The nesting depth is the
  count of `datumq`.

## Phase 2: the `-'` reader shorthand

`-'foo` reads as `(datumq foo)`: put the raw form `foo` here. `-'-'foo`
is the nested form above.

`-` becomes a non-terminating event macro whose predicate admits it
only when the next character is `'`. Every other `-`, as in `-1`, `->`,
`-foo`, and a lone `-`, defers to `_read_default` and reads as today; a
`-` inside an atom never dispatches. `-'x` read as `-` then `'x` before;
negating a quoted value means nothing, so no code relies on it, and the
long form `(- 'x)` stays available.

This is the one-predicate form of a two-character trigger. Under
[reader-macro-triggers.md](../reader-macro-triggers.md) it becomes a `-'`
registration and the predicate goes away.

## Touch points

Phase 1:

- `koskari/hygiene.py`: the directive cases in
  `expand_template_with_hygiene`.
- `docs/guide/hygiene.md` and `docs/guide/macros-and-hygiene.md`: the
  directives in templates, their nesting, and the backtick rule.
- Corpus records for `(datumq foo)`, a literal
  `((datumq datumq) ...)`, and the nested form.
- The scheduler variety macros may write `(datumq default/<type>)`
  directly; the dispatcher slot-passing stays valid either way.

Phase 2:

- `koskari/reader/parser.py`: the `-` registration and its reader.
- `koskari/lexer/tokens.py` and the editor grammars (tree-sitter,
  textmate, emacs): `-'` as a prefix.
- `docs/reference/grammar.md`: the shorthand.
- Corpus records for `-'foo` and `-'-'foo`.
