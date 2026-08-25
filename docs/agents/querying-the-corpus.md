# Querying the Koskari corpus MCP

The corpus holds verified example records (machine-checked at the read,
expand, eval, and error phases), documentation pages, and complete
example programs;
search is token-based, so query phrasing determines what you find.

---

## Tools and the two-hop flow

- `search_docs` -- full-text search over documentation pages and
  example programs; `kind: "doc"` or `"example"` restricts.
- `search_examples` -- search verified corpus records only; scope with
  `feature_path`, narrow with `polarity`.
- `get_reference` -- fetch one page or program by path.
- `get_example` -- fetch one corpus record by id.
- `list_features` -- enumerate feature paths with counts.
- `list_tags` -- enumerate tags, split into intent and structural
  facets, with counts.
- `corpus_version` -- the baked snapshot's git ref and row counts.

The standard loop is two hops: search to locate a path, then
`get_reference` for the full content. Documentation paths look like
`reference/specials/compile-define.md`; program paths start with
`examples/`.

## Feature paths

Corpus records are keyed by feature path. The families:

- `special/compile/` -- special forms (`special/compile/cond`)
- `macro/compile/` -- macros (`macro/compile/for-each`)
- `layout/` -- layout block forms (`layout/if`, `layout/define-class`)
- `reader/` -- reader syntax (`reader/#dict`, `reader/layout`)
- `builtin/` -- builtins (`builtin/demand`)
- `module/` -- library modules (`module/koskari.solve`)
- `runtime/` -- runtime behaviors (`runtime/range`)

A `feature_path` argument prefix-matches, so `layout/` scopes to every
layout form. `list_features` enumerates what exists.

## Polarity

`polarity: "valid"` returns only canonical usage; `polarity: "invalid"`
returns only verified wrong-usage records -- each documents a mistake,
why it fails, and the correct form. When a form misbehaves, search the
invalid records for it first: the failure is often already documented.

## Tags

`list_tags` splits the vocabulary into a closed goal-based *intent*
facet (`iterate`, `destructure`, `error-handling`, `pitfall`, ...) and
open *structural* tags (form names, `surface:block` vs `surface:sexpr`,
`pragma:` markers). Tag words match in `search_examples` queries, so
intent words are effective search terms on their own.

## What search matches

Documentation and program rows match query tokens against `path`,
`title`, and `content`. Corpus records match against `source`,
`feature`, `corpus_id`, and `tags`. Tokens are lowercase runs of
`[a-z0-9-]`; hyphens are kept, so hyphenated form names (`for-each`,
`define-class`) tokenize whole and are excellent query terms. Ranking
counts how many distinct query tokens a row contains. When no token
matches, a substring fallback runs, which is what lets short language
keywords (`if`, `and`, `while`) find examples at all.

Practical query crafting: name the mechanism, use the language's own
hyphenated names, and add one intent tag word. `for-each destructure`
beats `how do I loop over pairs`.
