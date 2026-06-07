# Koskari Design: Flat Layout as Default

## Status

Proposed design. Not implemented. Evaluate before changing reader fallback or
updating tests and corpus records.

## Summary

Change the default layout fallback for unknown block heads from
**`layout/block`** (wrap body in **`compile/begin`**) to **`layout/flat`**
(splice body forms after header args: **`(HEAD ARG … BODY …)`**).

Named layouts (**`layout/if`**, **`layout/try`**, **`layout/switch`**, …) stay
as they are unless a follow-up change is needed for children that today assume
the old default.

**`layout/block`** remains registered as an **opt-in** handler for heads that
want an explicit read-time **`(compile/begin BODY …)`** wrapper.

## Motivation

The original layout model treated the default block as implicitly bundling the
body like **`(HEAD … (begin BODY …))`**, implemented today as
**`(HEAD … (compile/begin BODY …))`** via **`layout/block`**.

In practice:

* Many specials and macros already expect a **tail of forms** after the header
  (e.g. **`compile/define`** with formals, **`compile/cond`** clauses, loop
  layouts that already splice flat).
* **`define layout/define-syntax layout/flat`** and **`define layout/extend-syntax
  layout/flat`** in **`koskari/bootstrap/_define_syntax.ki`** exist because
  wrapping the transformer in **`compile/begin`** at read time was wrong for
  those heads.
* Nested blocks under **unknown** heads often get an extra **`compile/begin`**
  layer that disturbs the shape parents expect (e.g. call-like tails, quasiquote
  fixtures, nested **`bar x:`** inside an outer block).

The flat default aligns read output with the early mental model:
**`(head … body …)`** without an automatic sequencer wrapper.

The compiler already sequences multiple body forms via **`helper_begin`** and
**`REST`** / **`BODY`** matchers; read-time **`compile/begin`** is not required
for most compiled code.

## Current behaviour

| Handler | Read result (multi-body) |
|---------|---------------------------|
| **`layout/block`** (default fallback) | **`(HEAD ARG … (compile/begin FORM …))`** |
| **`layout/flat`** | **`(HEAD ARG … FORM …)`** |
| **`layout/begin`** | Same object as **`layout/flat`**; registered for head **`begin`** |
| **`layout/if`**, **`layout/while`**, … | Handler-specific; peers and clause lists |

Resolution: **`find_layout`** / **`LayoutReader`**; if no **`layout/<head>`**,
invoke **`layout/block`**. See [LAYOUTS.md](../LAYOUTS.md) and
**`koskari/reader/layouts/basics.py`**.

## Proposed behaviour

| Role | Handler |
|------|---------|
| Default fallback | **`layout/flat`** (same implementation as today) |
| Explicit wrap | **`layout/block`** — opt-in via **`define layout/<head> layout/block`** |
| **`begin:`** blocks | **`layout/begin`** unchanged (**`(begin FORM …)`**, not **`compile/begin`**) |

Remove redundant Ki aliases once default is flat:

* **`define layout/define-syntax layout/flat`**
* **`define layout/extend-syntax layout/flat`**

(Or keep them as documentation-only aliases; they become no-ops relative to
default.)

## Expected benefits

* **Definitions and calls:** **`define name [args]:`** / unknown call-like blocks
  read as **`(define name [args] STMT …)`**, matching **`compile/define`**
  **`REST("body")`** patterns without an extra **`compile/begin`**.
* **`when-main:`**, **`define-route …:`**, and similar macros that use
  **`body …`** in **`syntax-rules`** rules.
* **Nested unknown heads:** e.g. **`foo …: bar x: (baz x)`** →
  **`(foo … (bar x (baz x)))`** instead of double **`compile/begin`** wrapping.
* **Macro expansion:** Less reliance on **`macroexpand`** stripping a redundant
  single-form **`compile/begin`** wrapper (**`koskari/bootstrap/basics.ki`**).

## Breaking areas and required fixes

These are **not** fixed by flipping the default alone. Each needs a designed
change before or with the switch.

### 1. **`layout/switch`** and inner **`case`** / **`else`**

**`layout_switch`** assumes children were processed by the default block handler:

* **`case COND:`** → **`(case COND (compile/begin BODY …))`**
* **`else:`** → **`(else (compile/begin BODY …))`**

It validates **`len(rest_parts) == 2`** per child. Flat children
**`(case COND BODY …)`** fail validation.

**Options:**

* Add **`layout/case`** and **`layout/else`** (flat or thin wrappers), or
* Teach **`layout_switch`** to accept flat tails and wrap clause bodies in
  **`compile/begin`** inside the handler (similar to **`layout_try`** for the
  main **`try:`** body).

Ref: **`koskari/reader/layouts/conditionals.py`**, **`tests/reader/layouts/test_switch_layout.py`**.

### 2. Indented **syntax-rules / macro-rules** rule blocks

**`define-syntax`** / **`extend-syntax`** blocks already use **`layout/flat`**.
Nested **rule** blocks (**`double x:`**, **`case (…):`**, …) still use the
default today, producing **`compile/begin`** in the template when the template
has one form.

**Multi-statement** templates depend on that wrap because
**`_pattern_template_from_rule`** in **`koskari/reader/layouts/define_syntax.py`**
sets **`template = rule_parts[-1]`** after flattening the rule spine. Flat read
of a rule with two body lines keeps only the last form as the template.

**Fix:** In **`layout_syntax_rules`** (and **`macro-rules`**, **`syntax-match`**,
siblings), split header vs body from the block **`source`** pair and build
**`(pattern (compile/begin BODY …))`** when **`len(body) > 1`**, instead of
relying on default **`layout/block`** on each rule.

Ref: **`tests/reader/layouts/test_define_syntax.py`** (e.g.
**`test_define_syntax_layout_parses`**).

### 3. Tests and corpus

Reader tests that assert default **`compile/begin`** wrapping must be updated,
including:

* **`tests/reader/test_layout.py`**
* **`tests/reader/test_layout_ng.py`**
* **`tests/reader/layouts/test_quasiquote.py`** (nested unknown-head blocks)
* **`tests/reader/layouts/test_apply_layout_runtime.py`**
* Corpus / docs that record read shapes with nested **`compile/begin`**

### 4. Documentation

* [LAYOUTS.md](../LAYOUTS.md) — default fallback section
* [SYNTAX.md](../SYNTAX.md) — default block handler
* [docs/reference/reader-layout.md](../../docs/reference/reader-layout.md) — normative layout syntax and default fallback

## Layouts unchanged by default flip

Handlers that already wrap or splice correctly at the outer level:

| Layout | Notes |
|--------|--------|
| **`layout/if`**, **`layout/while`**, **`layout/for-each`** | Flat clause bodies |
| **`layout/try`** | Wraps main **`try:`** body in **`compile/begin`** in handler |
| **`layout/class`**, **`layout/define-class`** | Outer **`(compile/begin BODY …)`**; inner **`define`** blocks become flat |
| **`layout/dyn-wind`** | Uses **`begin`**, not default block |
| **`layout/call`** | Flat **`compile/call`** |

## Suggested implementation order

1. Fix **`layout_switch`** (or add **`layout/case`** / **`layout/else`**).
2. Fix rule-template handling in **`define_syntax.py`** layouts.
3. Switch fallback in **`find_layout`**, **`apply_layouts`**, and
   **`LayoutReader`** to **`layout/flat`**.
4. Keep **`layout/block`** registered; document opt-in **`define layout/<head>
   layout/block`**.
5. Update tests, [LAYOUTS.md](../LAYOUTS.md), corpus records.
6. Revisit **`macroexpand`** / **`macroexpand-1`** strip heuristics in
   **`basics.ki`** if redundant wrappers disappear.

## Application **`.ki`** corpus (reconnaissance)

Existing **`.ki`** under **`examples/`**, **`tests/reproducers/`**, and
**`koskari/cli/repl.ki`** use **`define`**, **`define-class`**, **`if`** /
**`while`** / **`for-each`**, and s-expression macros. None use **`switch:`**
block syntax in those trees. A flat default is **unlikely to break** those
modules; risk is concentrated in layout tests, switch, and indented macro
rule blocks.

## References

* [LAYOUTS.md](../LAYOUTS.md) — handler reference
* [SYNTAX.md](../SYNTAX.md) — block mode and default handler
* [docs/reference/reader-layout.md](../../docs/reference/reader-layout.md) — layout reader model
* **`koskari/reader/layouts/basics.py`** — **`layout_block`**, **`layout_flat`**
* **`koskari/bootstrap/_define_syntax.ki`** — flat aliases for define-syntax
* [MACROS.md](../MACROS.md) — define-syntax layout notes

## Open questions

* Should **`layout/block`** rename or alias to **`layout/begin-block`** / similar
  to avoid confusion with **`layout/begin`** (**`begin`** vs **`compile/begin`**)?
* Should quasiquote / **`:block:`** expansion apply flat or block layout to
  inner heads when **`apply_layout`** runs at macro finalize time?
* Deprecation period: accept both read shapes during migration, or atomic test/doc
  update with the fallback change?
