# Corpus schema philosophy

This document clarifies how the Koskari **language conformance corpus** is
designed, how it relates to user documentation, regression tests, and
downstream machine-learning datasets. It augments the documentation plan in
[documentation-goals.md](documentation-goals.md) by specifying the corpus as a
shared source of truth for examples, verification, and generated artifacts.

**Not in scope here:** implementation of corpus tooling, CI wiring, or a
specific Hugging Face account or dataset slug. **In scope:** schema philosophy,
verification model, publication pipeline, and how LLM training datasets derive
from the corpus without driving its design.

---

## Corpus first, dataset second

The Koskari corpus is not primarily an AI training dataset.

It is a **language conformance corpus**.

The corpus exists to describe and verify language behavior.

AI and machine-learning use cases are **downstream consumers** of the corpus
rather than primary design drivers.

This distinction is important because AI dataset formats, training conventions,
and publication mechanisms will change over time, while the language
specification should remain stable.

The corpus should therefore be designed around **language semantics** rather
than model-training requirements.

---

## Relationship between corpus and other artifacts

The intended relationship is:

```text
                    Corpus
                       │
            ┌──────────┼──────────┐
            │          │          │
            ▼          ▼          ▼
         Tests      Docs      Datasets
```

The corpus is the source of truth.

Generated documentation, regression tests, and published datasets should derive
from the corpus rather than maintaining separate representations.

This aligns with [documentation-goals.md](documentation-goals.md) § 6
(maintenance and single sources of truth): tutorial examples, reference
snippets, and grammar illustrations should prefer corpus-backed records over
hand-maintained duplicates. User docs remain learner-oriented in tone; the
corpus supplies **canonical examples and expected behavior** that docs and
tests consume.

---

## Existing schemas

There is no widely adopted schema that directly models the needs of a
programming language conformance corpus.

Existing schemas generally fall into three categories.

### Documentation formats

Examples:

* Markdown
* Sphinx examples
* Language specification examples

These are human-oriented and generally lack machine-verifiable semantics.

### Machine learning datasets

Examples:

```json
{
  "prompt": "...",
  "completion": "..."
}
```

```json
{
  "instruction": "...",
  "input": "...",
  "output": "..."
}
```

```json
{
  "text": "..."
}
```

These schemas are designed around model training rather than language behavior.

### Testing formats

Examples:

* JSON Schema tests
* ECMAScript Test262
* Python parser tests
* Compiler conformance suites

These are closer to Koskari's needs but remain implementation-specific.

No existing format directly captures:

```text
Source
→ Reader
→ Layout
→ Expansion
→ Compilation
→ Runtime
→ Failure Conditions
```

as a unified record.

---

## Custom schema

The corpus should define a Koskari-specific schema.

This schema should be documented in:

```text
docs/corpus/SCHEMA.md
docs/corpus/SCHEMA.json
```

where:

* `SCHEMA.md` describes the intent and semantics
* `SCHEMA.json` provides machine validation

The schema exists to validate **corpus structure** rather than language
behavior.

Language behavior is validated through **corpus execution**.

When the physical `docs/` tree described in [documentation-goals.md](documentation-goals.md)
is bootstrapped, `docs/corpus/` is the public home for schema files and
generated corpus indexes; this design note remains the rationale under
`design/`.

---

## JSON Schema usage

JSON Schema should be used only for **structural validation**.

Examples:

* required fields
* field types
* enum values
* shape constraints

Example record (structural shape only):

```json
{
  "id": "layout-if-basic-001",
  "feature": "layout/if",
  "source": "...",
  "verify": {}
}
```

JSON Schema should **not** attempt to encode language semantics.

For example:

```json
{
  "verify": {
    "read": "(compile/cond ...)"
  }
}
```

is structurally valid regardless of whether the expected form is correct.

Semantic verification belongs to the corpus test runners.

---

## Recommended record structure

The schema should reflect **language phases** rather than AI concepts.

Preferred:

```json
{
  "id": "layout-if-basic-001",

  "feature": "layout/if",

  "source": "if x:\n  y",

  "verify": {
    "read": "(compile/cond (x y))"
  },

  "references": [
    "LAYOUTS.md#layout-if"
  ]
}
```

Not preferred as the **canonical** corpus shape:

```json
{
  "instruction": "...",
  "input": "...",
  "output": "..."
}
```

The latter is useful for model training but does not accurately represent
language behavior as the authoritative record.

Optional corpus fields may support downstream generation without becoming the
primary model:

* `tags` — feature area, stability label, difficulty
* `notes` — maintainer-facing rationale (not copied verbatim into training rows)
* `stability` — `stable`, `evolving`, or `experimental`, consistent with
  [documentation-goals.md](documentation-goals.md) stability labels

---

## Verification model

The corpus should describe expectations at **language boundaries**.

Examples:

```json
{
  "verify": {
    "read": "...",
    "expand": "...",
    "compile": "...",
    "eval": "..."
  }
}
```

Each verification stage corresponds to an actual Koskari subsystem.

This mirrors the implementation architecture:

```text
Reader
Layout
Matcher
Macro Expansion
Compiler
Evaluator
```

and allows corpus records to remain meaningful even as publication formats
evolve.

Failure conditions belong in the same model: expected error kind, message
pattern, or stage at which processing stops. Records that specify only success
paths should say so explicitly (for example via `verify.error` or a dedicated
`expect: failure` envelope once the schema is finalized).

---

## Dataset publication

Published datasets should be **generated from** corpus records.

Examples:

```text
Corpus JSONL
      │
      ▼
 Transformation
      │
      ▼
Parquet (optional)
      │
      ▼
Hugging Face Dataset
```

The dataset schema may differ from the corpus schema.

For example:

```json
{
  "source": "...",
  "expanded": "...",
  "feature": "layout/if"
}
```

might be generated from a richer corpus record.

The corpus should not be constrained by the needs of a specific publication
target.

---

## Hugging Face and LLM training targets

Hugging Face (and similar hubs) are **publication sinks**, not schema
authorities. The goal is to produce datasets that help LLMs **read, expand,
and reason about Koskari syntax** — surface forms, layout blocks, macro
expansion, and common idioms — while keeping the conformance corpus
implementation-neutral.

### Design constraints

1. **One corpus, many views.** Each Hugging Face dataset is a *view* produced
   by a documented transform. Transforms live in tooling (for example under
   `tools/corpus/` or a tox env), not in hand-edited dataset files in the
   repo.
2. **Provenance.** Every generated row should trace to a corpus `id` (and
   corpus revision or git ref when published). Downstream users can join back
   to full verification metadata.
3. **No semantic drift.** Training rows must not assert behavior that the
   corpus does not verify at the relevant stage. If only `read` is verified,
   a completion-style dataset should not invent `eval` results.
4. **License and attribution.** Publication metadata should match the project
   license and cite the corpus schema version.

### Example transform targets

These are **illustrative output shapes**, not canonical corpus records.

**Completion / fill-in (syntax → next stage):**

```json
{
  "prompt": "Given Koskari source, show the read form:\n\nif x:\n  y",
  "completion": "(compile/cond (x y))",
  "corpus_id": "layout-if-basic-001",
  "feature": "layout/if",
  "stage": "read"
}
```

**Instruction tuning (multi-field):**

```json
{
  "instruction": "Expand this Koskari layout block to its s-expression read form.",
  "input": "if x:\n  y",
  "output": "(compile/cond (x y))",
  "corpus_id": "layout-if-basic-001"
}
```

**Plain text pretraining chunk:**

```json
{
  "text": "# Koskari example: layout/if\n\nSource:\nif x:\n  y\n\nRead form:\n(compile/cond (x y))\n",
  "corpus_id": "layout-if-basic-001"
}
```

**Multi-stage trace (when corpus verifies multiple boundaries):**

```json
{
  "source": "if x:\n  y",
  "read": "(compile/cond (x y))",
  "expand": "...",
  "feature": "layout/if",
  "corpus_id": "layout-if-basic-001"
}
```

Transforms may emit several Hugging Face dataset configurations from one
corpus snapshot (for example `koskari-read`, `koskari-expand`, `koskari-text`).

### Suggested documentation layout (when `docs/` exists)

| Path | Summary |
|------|---------|
| `docs/corpus/SCHEMA.md` | Human-readable corpus schema and verification semantics |
| `docs/corpus/SCHEMA.json` | JSON Schema for corpus records |
| `docs/corpus/README.md` | How to run corpus tests, add records, and regenerate views |
| `docs/corpus/datasets.md` | Hugging Face dataset cards, transform descriptions, and column glossaries |

Dataset cards should explain **what was verified** in the source corpus, not
only what appears in training columns.

### Coupling to user documentation

Corpus records with `references` pointing at design or future user-doc anchors
(for example `LAYOUTS.md#layout-if`, or later `reference/reader-layout.md`)
create a bidirectional link:

* Docs and tutorials can embed or link corpus-backed examples.
* LLM datasets inherit the same examples with machine-readable stage labels.
* Tests and docs stay aligned because both consume the corpus.

See [documentation-goals.md](documentation-goals.md) § 4.5 (`reference/`) and
§ 6 for where generated snippets should attach in the public doc tree.

---

## Long-term goal

The corpus should be understandable even if all AI-related publication targets
disappear.

A future maintainer should be able to read a corpus record and understand:

* what language behavior is being specified
* what subsystem is being verified
* what result is expected

without needing to understand any machine-learning tooling.

This requirement should guide schema evolution decisions.

---

## See also

* [documentation-goals.md](documentation-goals.md) — user documentation
  information architecture and maintenance rules
* [LAYOUTS.md](LAYOUTS.md) — layout system reference (likely corpus `feature`
  and `references` target)
* [SYNTAX.md](SYNTAX.md) — syntax design reference
* [SPECIALS.md](SPECIALS.md) — special forms reference
* [MACROS.md](MACROS.md) — macro reference
