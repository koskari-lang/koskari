# Corpus record schema

Corpus records describe language behaviour at phase boundaries. JSON Schema
validates **structure only**; semantics are checked by corpus test runners.

## Recommended record

```json
{
  "id": "layout-if-basic-001",
  "feature": "layout/if",
  "source": "if x:\n  y",
  "verify": {
    "read": "(compile/cond (x y))"
  },
  "references": [
    "reference/layouts/layout-if.md"
  ]
}
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

**references**
: Links to pages under `docs/` (reference, start, guide, etc.).

**stability** (optional)
: `stable`, `evolving`, or `experimental`.

**tags**, **notes** (optional)
: Maintainer metadata; not copied verbatim into training exports.

See [SCHEMA.json](SCHEMA.json) for machine validation.

## Source conventions

A single source form must not **define a syntax or macro transformer and invoke
that name in the same expression**. Do not wrap `(define-syntax NAME …)` or
`(define NAME (macro-rules …))` (or other transformer-binding `define`) together
with a call to `NAME` in one `(begin …)`.

Install transformers and use them in separate top-level forms (or separate
compilation units). Use-site behaviour belongs in `phase_specials` tests under
`tests/compile/specials/` when a corpus record cannot pre-bind the name.

## Invalid source shards (`*-invalid.jsonl`)

Some records document **illegal Koskari** — expressions that must not appear in
real programs. Keep them in companion shards such as
`variables-invalid.jsonl` or `conditionals-invalid.jsonl`, not in the primary
shard for that topic.

Use **`verify.invalid`** with a prose explanation of why the source is wrong.
Those records are schema-validated but **not** run through read/expand/compile/eval
verification (unlike `verify.error`, which asserts a pipeline failure).

**Exception:** invalid records may also include **`verify.read`** (or other
stages) when the point is to pin the **surprising successful parse** while
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
`42` in a slot uses `define`, `local`, or `setq` — not `(bind! y 42)`.

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
| `improper-list:(1 2 . 3)` | Improper list shape |

## Compile value conventions (seed records)

`verify.compile` is optional. Use it only for **author-visible** compile
outcomes that an agent writing Koskari source would care about:

| Form | Meaning |
|------|---------|
| `__annotations__ return int` | Built function carries return annotation |
| `code.extras return annotation int` | Lambda code object records return type |
| `special:class yields KosType named C` | Class special defines a KosType |
| `macro expands to define + class; yields KosType C` | define-class macro result |
| `single fn unchanged` | Macro compose identity on one function |

**Forbidden in corpus records:** opcodes, bytecode shape, disassembly, tail-call
opcode choice, constant-folding internals, or any other VM implementation detail.
That coverage belongs in `phase_specials` unit tests under `tests/compile/`,
not in JSONL shards.
