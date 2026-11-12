# Corpus record schema

Corpus records describe language behaviour at phase boundaries. JSON Schema
validates **structure only**; semantics are checked by corpus test runners.

## Recommended record

Shards are multi-document YAML under `records/`. Each record is one document
separated by `---`. Use a literal block scalar (`|-`) for multi-line `source`;
keep single-line s-expressions inline.

```yaml
---
id: layout-if-basic-001
feature: layout/if
source: |-
  if x:
    y
verify:
  read: (cond (x y))
references:
  - reference/layouts/layout-if.md
```

## Fields

**id**
: Unique record identifier.

**feature**
: Feature path (e.g. `layout/if`, `special/compile/cond`).

**source**
: Koskari source text.

**verify**
: Expected results at one or more phases: `read`, `expand`, `compile`, `eval`,
  `error`, or `invalid`.

**verify.warning** (optional)
: The warning the eval stage must emit, in the `verify.error` shape:
  `"<WarningClass>: <message substring>"`, for example
  `"RuntimeWarning: coroutine '<lambda>' was never awaited"`. A record without
  it must emit no warning during eval. It qualifies the eval stage and is not a
  stage of its own.

**verify.gc_collect** (optional)
: A boolean. `true` runs a full garbage collection after the eval, before the
  warnings are checked. Set it on a record whose warning is emitted when a
  dropped object is finalized. Absent or `false`, no collection runs. It
  qualifies the eval stage and is not a stage of its own.

**references**
: Links to pages under `docs/` (reference, start, guide, etc.).

**stability** (optional)
: `stable`, `evolving`, or `experimental`.

**tags**, **notes** (optional)
: Maintainer metadata. `tags` ship to the retrieval (`corpus`) export and are
  full-text indexed, so they are a search channel, not just labels; the
  training (`eval`) projection drops them. See **Intent tags** below.

See [SCHEMA.json](SCHEMA.json) for machine validation.

## Tags: structural and intent

Two facets share the one `tags` list, and both are full-text searchable:

- **Structural tags** answer *what form is this* — the category
  (`object`, `special`, `macro`, `variable`, `iter`, …), the form name
  (`try`, `for-each`, `cond`), the surface (`surface:block`,
  `surface:sexpr`, `surface:line`), the pragma a record depends on
  (`pragma:reader/dot-attrs`), and polarity (`invalid`, `surprising`).
- **Intent tags** answer *what am I trying to do* — the goal words a
  newcomer types before they know the form's name. Add them so a search
  for `cleanup` or `catch` finds `try`/`finally` even though neither word
  appears in the source.

Keep intent tags to this closed vocabulary (extend it deliberately, not
per-record):

| Domain | Intent tags |
|---|---|
| Error handling | `error-handling`, `catch`, `cleanup`, `re-raise`, `guard` |
| Looping | `loop`, `iterate`, `accumulate`, `early-exit`, `filter`, `destructure` |
| Binding & scope | `mutate`, `swap`, `module-global`, `local-binding`, `name-resolution`, `recursion`, `default-value` |
| Control flow | `branch`, `multi-way`, `short-circuit` |
| Functions | `higher-order`, `closure`, `variadic`, `compose`, `decorate`, `keyword-argument` |
| Data | `construct`, `index`, `membership` |
| Async | `await`, `concurrency`, `suspend` |
| Classes | `define-type`, `inheritance`, `predicate` |
| Metaprogramming | `metaprogramming`, `hygiene` |
| Pedagogy | `idiom`, `pitfall` |

`metaprogramming` marks records that teach *writing* macros (syntax,
syntax-rules, quasiquote/unquote, gensym, special-match) — not every builtin
that happens to live under a `macro/compile/…` feature path.

### Pragma tags

A record whose syntax exists only because a pragma setting is on carries
`pragma:<setting>`, one per setting it relies on, and says so in `notes` as
well. Reading a record that shows `seq[0]` is useless without knowing that
`reader/index-brackets` is what makes it read that way and that turning the
setting off changes the parse rather than raising. A record exercising two
gated features carries both tags.

```yaml
tags:
  - reader
  - index-bracket
  - pragma:reader/index-brackets
notes: "requires reader/index-brackets (baseline on). …"
```

## Source conventions

A single source form must not **define a syntax or macro transformer and invoke
that name in the same expression**. Do not wrap `(syntax NAME …)`, `(define-syntax NAME …)` or
`(var NAME (macro-rules …))` (or other transformer-binding `var`) together
with a call to `NAME` in one `(begin …)`.

Install transformers and use them in separate top-level forms (or separate
compilation units). Use-site behaviour belongs in `phase_specials` tests under
`tests/compile/specials/` when a corpus record cannot pre-bind the name.

## Invalid source shards (`*-invalid.yaml`)

Some records document **illegal Koskari** — expressions that must not appear in
real programs. Keep them in companion shards such as
`variables-invalid.yaml` or `conditionals-invalid.yaml`, not in the primary
shard for that topic.

Use **`verify.invalid`** with a prose explanation of why the source is wrong.
Those records are schema-validated but **not** run through read/expand/compile/eval
verification (unlike `verify.error`, which asserts a pipeline failure).

**Exception:** invalid records may also include **`verify.read`** (or other
stages) when the point is to record the **surprising successful parse** while
`verify.invalid` explains why the source must not be used. The harness skips
only the `invalid` key; other verify stages still run.

### Layout traps (`verify.invalid`, tag `surprising`)

Some invalid layout **reads successfully** — no `LayoutError`. Symbols such as
`else`, `elif`, and `then` are only special as **declared peers** after a block
`if:`; elsewhere they are ordinary call heads, like `(else "taco")`.

Records in this category document Python-looking surface syntax that parses as
**two expressions** or a **misleading call block**, with failure deferred to
**eval** (unbound name) rather than read. Do not use **`verify.error:
LayoutError`** for these; use **`verify.invalid`** plus optional **`verify.read`**
showing the actual lowered form.

Tag with **`surprising`** in addition to **`invalid`**.

Tag invalid records with **`invalid`** in `tags`. Prefer `references` to the
concept or macro page that states the correct rule (for example
`reference/concepts/bindings-and-refs.md`).

**`bind!` / `bindq`:** bind a name to a **Ref**, not to a plain value. Storing
`42` in a slot uses `var` or `setq` — not `(bind! y 42)`.

## Eval value conventions (seed records)

Until a corpus runner exists, `verify.eval` strings in seed records use an
informal encoding:

| Form | Meaning |
|------|---------|
| `42`, `"hello"`, `true`, `false`, `null` | JSON-like scalar results |
| `symbol:x` | Koskari symbol |
| `list:[1,2,3]` | Python list (from `list`, `#[…]`, or `list-each`) |
| `tuple:(1,2,3)` | Python tuple (from `tuple`, or `#(…)`) |
| `proper-list:(1 2 3)` | Koskari proper list (cons chain ending in nil) |
| `bracket-list:[1 2 3]` | Koskari proper bracket list (bracket-cons chain ending in bracket-nil) |
| `improper-list:(1 2 . 3)` | Improper list shape |

## Marked complex eval encodings

When the eval result is not a simple literal, prefix the `verify.eval` string
with `@`. The harness strips `@` and runs a registered complex check.

| Form | Meaning |
|------|---------|
| `@kostype:NAME` | Eval result is a Koskari-defined class named NAME |
| `@annotations:return:TYPE` | Eval result is callable whose `__annotations__['return']` **is** the builtin named TYPE (e.g. `@annotations:return:int` asserts the `int` type object, not the string `"int"`) |

**Forbidden in marked encodings:** opcodes, bytecode shape, disassembly,
tail-call opcode choice, constant-folding internals, or any other VM
implementation detail. That coverage belongs in `phase_specials` unit tests
under `tests/compile/`, not in corpus YAML shards.

## Expand stage conventions

`verify.expand` compares one macro-expansion step on the **outer** form head.
Use a source whose macro invocation is top-level (not wrapped in `begin`),
unless testing `begin` itself. Use `...` wildcards for irrelevant tail forms,
as in [syntax.yaml](records/syntax.yaml).
