# Koskari Unified Matcher & Transformer Model

**Status:** Design proposal; guides implementation. Parts of the matcher stack and
`syntax-rules` exist today (`koskari/matcher`, `koskari/compile/syntax.py`);
`macro-rules`, syntax boxes, and layered post-expand passes are not yet
implemented. Complements
[macro-hygiene-syntax-boxes.md](macro-hygiene-syntax-boxes.md).

**Completed in current step (`koskari/compile/syntax.py`):**

* `SyntaxRulesTransformer` now stores ordered `segments`, where each segment
  carries its own `literals` and `rules` table.
* `compile_impl` now dispatches by segment then rule (`first match wins`),
  preserving existing `define-syntax` behavior.
* `SyntaxRule.try_expand(...)` now encapsulates the per-rule
  match-and-expand path.
* `from_segments(...)` exists for future merge/prepend extension work.
* Tagged tuple wire format is unchanged for compatibility; `to_tuple` currently
  requires exactly one segment until a multi-segment pool format is introduced.

## Related documents

- **Syntax boxes / procedural hygiene**: [macro-hygiene-syntax-boxes.md](macro-hygiene-syntax-boxes.md)
- **Matcher hygiene metadata (current implementation)**: [matcher-hygiene-extensions.md](../api/matcher-hygiene-extensions.md)
- **Pattern matching API (tagged tuples, `koskari.matcher`)**: [pattern-matching.md](../api/pattern-matching.md)
- **Implemented compile-time macros**: [MACROS.md](../MACROS.md)
- **`compile/define-syntax` reference**: [SPECIALS.md](../SPECIALS.md) (compile/define-syntax)
- **Open work**: [TODO.md](../TODO.md) (Macro expansion; Procedural macro hygiene)
- **Historical macro notes**: [archive/macros.md](../archive/macros.md)

---

## Overview

Koskari's macro and syntax systems converge on a single underlying abstraction:

> A **transformer** is an **ordered rule table**. For each invocation, the
> implementation **matches** the input form against rules in order; the **first**
> successful match **applies an expansion worker**; the result passes through
> **shared post-processing** (hygiene, then unboxing).

This model unifies:

* **Template macros** — `syntax-rules` (pattern + template), refactored as one
  kind of expansion worker on the shared table.
* **Procedural macros** — `macro-rules` (pattern + body executed at expand time),
  the other kind of worker on the **same** table.
* **Python-defined special forms** — same matcher vocabulary (`koskari.matcher`)
  for structure and hygiene metadata; dispatch via `compile_impl` today is
  already a parallel entry point on the compiler.

The **discriminator** between declarative and procedural authoring is the **inner
form** (`syntax-rules` vs `macro-rules`), not necessarily a second defining
keyword (see [Binding name: define-syntax only](#binding-name-define-syntax-only)).

---

## Implementation architecture (compiler protocol)

### Duck-typed transformer entry

The compiler resolves the head of a form with `find_compiled` and invokes
`compile_impl(compiler, source_obj, tc)`. Any object that provides
`compile_impl` participates in the same path as specials and
`SyntaxRulesTransformer` (`koskari/compile/compiler.py`: `compile_pair` →
`compile_special`). If `compile_impl` returns a **cons tree** (not a
`ByteCodeFragment`), `full_compile` re-enters compilation — that loop is the
**macro expansion driver**.

**Implication:** New transformer types (`MacroTransformer`, a refactored base) do
not require new dispatch machinery; they **overload** `compile_impl` (and/or
delegate to a shared implementation).

### Base abstraction: one algorithm, typed workers

At the lowest shared layer:

1. **Rule table** — Ordered list of rules. Each rule has:

   * a **pattern** (or matcher specification) for the invocation shape;
   * an **expansion worker** — either **template** or **procedure**;
   * optional **per-rule metadata** (literals subset, provenance, guards).

2. **Single dispatch loop** — `compile_impl` (or equivalent):

   * walk rules in order;
   * **match** input against the pattern;
   * on first success, **apply expansion** for that rule only;
   * return expanded source for the outer compile loop.

3. **No hygiene inside the worker loop** — Template expansion and macro bodies
   return **candidate** source. **Hygienic rewriting** and **unboxing** are
   **post-expand stages** shared by both worker kinds.

Concrete types (names indicative):

| Type | Role |
|------|------|
| **Base / `RuleTableTransformer`** (conceptual) | Holds ordered rules; implements the match → expand loop; delegates each row to a small **strategy** object. |
| **`SyntaxRulesTransformer`** (refactor target) | Rules whose RHS is **template expansion** (substitution, ellipsis, existing binding-symbol discovery for introduced names). |
| **`MacroTransformer`** | Rules whose RHS is a **procedure** (expand-time lambda / bridge), fed match results per the [formal binding contract](#formal-binding-contract-macro-rules). |

Both are **one datatype at the protocol level**: an ordered heterogeneous table.
Serialization (const pool / `build-syntax` family) should carry **per-rule tags**
so loaders can reconstruct the correct worker type.

### Layered pipeline (after expansion)

For every rule, after the worker returns:

1. **Hygiene transform** — Matcher-informed gensym / binding isolation (reuse
   and extend `_collect_binding_symbols_matcher` and related paths for
   templates; align procedural output with [macro-hygiene-syntax-boxes.md](macro-hygiene-syntax-boxes.md) when boxes land).

2. **Unboxing / splice** — Resolve **syntax boxes**; merge call-site segments
   into the final tree. Template-only macros may omit boxes initially;
   procedural macros are the primary consumer.

Schematic order: **match → expand (template *or* macro) → hygiene → unbox → …**

---

## Core pipeline (per invocation)

1. **Pattern dispatch** — Rules run in **source order**; first match wins across
   the **entire** flattened table (including mixed `syntax-rules` and
   `macro-rules` sections).

2. **Match environment** — Named capture map (and optional metadata: repetition,
   scope roles for decorated patterns, boxing hints).

3. **Expansion** — Delegates to the rule's worker:

   * **Template worker** — existing template expander semantics;
   * **Macro worker** — invoke procedure with arguments derived from captures.

4. **Hygiene** — Shared post-expand pass.

5. **Unboxing** — Shared final splice when syntax boxes are implemented.

---

## Matcher IR (target vs current)

The **Matcher IR** in this document is the **target** shape for unifying
syntax-rules patterns, specials, and (eventually) one matching engine. Today:

* **Specials** use **`koskari.matcher`** (`MATCH`, `SYMBOL`, `VALUE`, `REST`,
  `LITERAL_*`, `BINDINGS`, scope metadata, etc.) — see
  [pattern-matching.md](../api/pattern-matching.md).
* **`syntax-rules` rule patterns** are matched by **`_match_pattern`** in
  `koskari/compile/syntax.py`, not yet lowered to the same IR as specials.

### Primitive matchers (conceptual / alignment)

* `SYMBOL(name)`
* `VALUE(name)`
* `LITERAL(sym)`
* `LITERAL_KW(kw)`
* `REST(name)`

### Decorated matchers (advanced / specials)

* `FORMALS(name, scope=...)`
* `BINDING(name, scope=...)`
* `USE(name)`
* `TYPE_EXPR(name)`
* `ATTR_PATH(name)`

### Composition

* `MATCH(...)` — ordered sequence
* Optional future: `OPTIONAL(...)`, `ONE_OF(...)`, `REPEAT(...)` at the unified
  IR level for macro patterns.

Surface syntaxes (`syntax-rules` lists, layout blocks, future `match` / `rule`
clauses) **lower** into this IR over time; v1 may keep `_match_pattern` for
syntax-rules while sharing **post-expand** and **metadata** conventions.

---

## Surface syntax: `syntax-rules` and `macro-rules`

### `syntax-rules` (declarative)

Each clause: **pattern + template**. The expander performs substitution and
ellipsis repetition; introduced binders are handled via existing template
hygiene (e.g. `_collect_binding_symbols_matcher` + gensym in
`koskari/compile/syntax.py`).

### `macro-rules` (procedural)

Each clause: **pattern + body**. The body is interpreted as the body of a
**lambda** whose **formal parameters are the pattern variables** bound by that
pattern — the same names link the left and right sides.

On match, the implementation conceptually applies:

`(lambda <pattern-vars…> . <body>)`

to the **captured subtrees** (with a **fixed policy** for ordering and for
ellipsis — see below). The body runs at **expand time** and returns a **source
form** (cons tree), optionally containing syntax boxes.

### Binding name: define-syntax only

Prefer **one** defining form, e.g. **`define-syntax` / `compile/define-syntax`**,
with a **discriminated right-hand side**:

* `(define-syntax NAME (syntax-rules …))`
* `(define-syntax NAME (macro-rules …))`
* `(define-syntax NAME (transformer …))` — wrapper that flattens multiple
  sections (below)

A separate **`define-macro`** keyword is **optional sugar** for documentation or
grep clarity only; it carries **no extra power** if `macro-rules` already marks
procedural rules. Recommendation: **standardize on `define-syntax` + inner head**
unless a strong human-factors reason appears.

### Formal binding contract (`macro-rules`)

The specification must pin down:

* **Which names are formals** — Pattern variables only; literals and the macro
  name do not become parameters.
* **Order of formals** — e.g. left-to-right, depth-first traversal of the
  pattern tree (exact rule TBD and documented).
* **Ellipsis** — Repeated captures map to a **single formal** whose value is a
  **list** of matched fragments (or another single consistent rule).
* **Implicit bindings** — Optional access to `compiler`, the whole form, or
  source positions: either **extra hidden parameters** or a fixed expand-time
  environment around the body.

---

## Combining sections: merge, extend, copy

**Extension** (copy base transformer, add rules) is implemented as **merging
ordered rule lists**, not as a separate inheritance mechanism.

* Multiple **`syntax-rules`** blocks, **`macro-rules`** blocks, or **mixed**
  sections under one definition flatten into **one** linear `rules` array in a
  **documented order** (recommended: **as written**, top to bottom).

* **First match wins** globally over the merged list — so precedence is
  controlled entirely by **concatenation order** (prepend vs append conventions
  for `extend-transformer` must be explicit).

* **Composable operators** (`transformer`, `extend-syntax`, `prepend-rules`,
  `append-rules`, `replace-rule`) are **constructors** of that merged list, not
  alternate runtime types. Optional **provenance** on each rule (which section or
  base it came from) aids debugging.

### Literals when merging multiple `syntax-rules`

Each `syntax-rules` form carries a **literals** set. When **flattening** two or
more blocks:

* **Robust:** Each rule (or each contiguous chunk) retains the **literal set**
  that applied when it was written, **or**
* **Simpler:** One **union** of literals for the whole transformer, with author
  discipline so patterns remain unambiguous.

Pick one policy and document it; **per-chunk literals** avoids subtle clashes
where a symbol is literal in one block and a pattern variable in another.

---

## Advanced / future surface: `match` and `rule` clauses

Optional higher-level clause forms (for decorated matchers, explicit scope
roles) may sugar the same underlying rule table. Illustrative shapes from the
earlier sketch:

```scheme
match (if cond then else):
  (compile/cond ...)
```

```scheme
rule (lambda name :: return_type formals body ...):
  ...
```

These are **not** required for v1; **`syntax-rules`** and **`macro-rules`**
remain the primary user-facing mirrors.

---

## Python specials

Special forms stay **authoritative** for compiler builtins. They already use
`koskari.matcher` for parsing and hygiene discovery. Long term, **pattern
dispatch** for macros and **matcher metadata** for specials should share
vocabulary and conventions; full unification of **one** matching engine for both
syntax-rules patterns and every special is a **refactor milestone**, not a
prerequisite for `macro-rules` on the shared **rule table + post-expand**
protocol.

---

## Outcome contract (expansion workers)

**Input:** Match environment (named captures; optional metadata).

**Output:** Source form tree; may include **syntax boxes** for procedural
workers.

Template and macro workers share this contract **up to** the post-expand
layers. **Hygiene** and **unbox** are **not** duplicated inside each worker type.

---

## Hygiene model

* Matcher / template metadata drives **which** symbols are in binding position
  for **template** expansion.
* Procedural macros may rely on **syntax boxes** + final splice (see
  macro-hygiene-syntax-boxes) and/or explicit **use-gensyms**-style discipline
  until the box pipeline is complete.

Future: lean more on matcher-declared binding metadata and less on ad hoc
preview heuristics.

---

## Syntax boxes

Single box type: preserves call-site segments, skips premature transformation,
**unboxed** in the final post-expand phase. Primary consumer: **macro** workers
mixing generated skeleton with user subforms. See
[macro-hygiene-syntax-boxes.md](macro-hygiene-syntax-boxes.md).

---

## Extension example (merged table)

```scheme
(define-syntax debug-cond
  (transformer
    (prepend
      (macro-rules ()
        ((cond [debug: x] body ...)
         ...)))
    base-cond-transformer-spec))
```

Conceptually: **prepend** builds a new ordered rule list (procedural clause
first, then all rules copied from `base-cond`). Exact surface syntax for
`transformer` / `extend-syntax` is TBD; semantics are **list merge + shared
dispatch**.

---

## Design principles

1. **Single dispatch loop** — One **match → expand** algorithm; workers vary
   only in **how** they expand.

2. **Shared post-expand** — Hygiene and unboxing are **layers**, not forked
   logic per macro style.

3. **`syntax-rules` / `macro-rules` symmetry** — Same rule-table shape;
   declarative vs procedural is the **RHS worker**.

4. **Merge for extension** — Copy, prepend, append = **construct ordered rule
   lists**; first match wins globally.

5. **Compiler protocol first** — `compile_impl` + cons-tree return integrates
   with existing `full_compile` behavior.

6. **Matcher-driven hygiene** — Binding semantics from metadata where possible.

7. **Progressive surface** — Ship `macro-rules` and merged sections before
   optional `match` / `rule` sugar.

---

## Open questions

* Formal **ordering** of `macro-rules` parameters for nested patterns.
* **Ellipsis** mapping to one formal vs split rules.
* **Implicit** expand-time bindings (`compiler`, `&whole`, source locations).
* Literals policy: **per-chunk** vs **global union** when merging `syntax-rules`.
* **Serialization** format for heterogeneous rule rows in the const pool.
* **Expand-time evaluation** for `macro-rules`: host Python callables first vs
  Koskari bytecode + eval bridge.
* Debugging: which rule matched, which section it came from.
* Optional `match` / `rule` decorated syntax and matcher algebra exposure.

---

## Summary

Koskari's macro system is a **matcher-driven rule table** with **pluggable
expansion workers** (template vs procedural), **shared** hygienic rewriting and
**unboxing**, and **merge-based extension**. Authoring centers on
**`syntax-rules`** and **`macro-rules`** under **`define-syntax`** (with optional
`define-macro` sugar). Python specials remain the compiler's native interface
while sharing matcher vocabulary and post-expand policy where possible.
