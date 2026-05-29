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
  or `error`.

**references**
: Links to user docs or design anchors.

**stability** (optional)
: `stable`, `evolving`, or `experimental`.

**tags**, **notes** (optional)
: Maintainer metadata; not copied verbatim into training exports.

See [SCHEMA.json](SCHEMA.json) for machine validation.

## Eval value conventions (seed records)

Until a corpus runner exists, `verify.eval` strings in seed records use an
informal encoding:

| Form | Meaning |
|------|---------|
| `42`, `"hello"`, `true`, `false`, `null` | JSON-like scalar results |
| `symbol:x` | Koskari symbol |
| `proper-list:[1,2,3]` | Proper list via unpack |
| `improper-list:(1 2 . 3)` | Improper list shape |
| `opcode:NAME count:N` | Expected bytecode shape at compile stage |
