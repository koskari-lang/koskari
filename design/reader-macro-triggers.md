# Koskari Design: multi-character reader macro triggers

## Status

Concept only, recorded 2026-09-24 during the hygiene rework. Not
planned for branch `hygiene-rework`.

## Problem

`Reader.add_event_macro(char, fn, terminating, predicate)` keys a
handler stack on one character. A trigger longer than one character
is spelled by hand inside a handler that peeks past its own character
and branches:

- `_read_collection` in `koskari/reader/parser.py` owns `#` and peeks
  one character to choose among `#'`, `` #` ``, `#(`, `#[`, and `#{`.
- `koskari/reader/layout.py` registers a second `#` event macro whose
  predicate peeks two characters to admit `#'head:` and `` #`head: ``
  as block prefixes, and its plain-quote sibling does the same for `'`
  and `` ` `` by another route.
- `koskari/lexer/tokens.py` repeats the `#'` and `` #` `` cases in the
  token scanner so the REPL and LSP colour them.

Three places know that `#` may be followed by a quote. Adding `#!` or
`#:` today means a new branch in the dispatcher, a new arm in the
layout predicate, and a new case in the lexer, and the dispatcher's
branch order is the only statement of precedence.

## Concept

A registration keyed by the trigger string, with the single-character
form as its one-character case:

```python
reader.add_event_macro("#'", read_quote_syntax, terminating=False)
reader.add_event_macro("#`", read_quasiquote_syntax)
reader.add_event_macro("#(", read_tuple_collection)
```

Dispatch stays keyed on the first character. Under it the reader holds
the registered triggers that start with that character, longest first,
and matches by peeking the length of the longest. A trigger that
matches wins over a shorter one; a handler that returns `DEFER` yields
to the next trigger of the same length, then to the shorter ones, as
the stack does today. The handler receives the whole trigger as its
`char` argument, which `_read_quote` and `_read_quasiquote` already
take for the reader's error text.

The trigger table is data the other two consumers can read: the layout
reader's block-prefix predicate asks whether the text at the cursor
begins with a registered quote trigger instead of peeking for `'` and
`` ` `` itself, and the lexer derives its prefix tokens from the same
table so a trigger is added once.

## Open

- Longest match against explicit precedence: whether a registration
  may declare that it wins over a longer trigger, or whether the length
  rule is the whole story.
- How an atom-boundary predicate composes with a multi-character
  trigger, since the boundary split looks at one terminating character.
- Whether the collection openers `#(`, `#[`, and `#{` move onto the
  table or stay as the `#` handler's own branches.
- Whether the suffix macro registry gets the same treatment.
