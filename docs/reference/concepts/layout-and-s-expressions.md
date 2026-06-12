# Layout and s-expressions

How Koskari's layout surface relates to parenthesized s-expressions, and why
both exist in the same language.

---

## Introduction

Koskari modules in `.ki` files are read by the **layout reader** by default.
At the top level of a file (outside any `(`, `[`, or `{`), you write
indentation-based blocks and lines. The reader turns that text into the same
kind of **form trees** that explicit s-expressions produce — lists built from
pairs — and the compiler, macro expander, and evaluator see no difference
after read.

Parenthesized s-expressions remain fully supported. You can embed them anywhere
delimiters allow, and you can write entire programs with parentheses only. Layout
is optional authoring sugar on top of the s-expression core, not a separate
language.

For normative syntax rules, see [Reader and layout syntax](../reader-layout.md).

---

## YAML and JSON

A useful mental model compares layout mode to YAML and s-expression mode to JSON.

At the **document level**, Koskari layout behaves like YAML: indentation
matters, block headers end with `:`, and bodies sit on following indented lines.
When you open `(`, `[`, or `{`, you enter an **s-expression island** — nested
forms follow delimiter rules only, the way a `{ ... }` or `[ ... ]` flow
collection in YAML follows JSON-like rules inside the braces. Physical newlines
inside balanced delimiters do not end the outer header or expression.

Koskari is a **syntax superset of s-expressions** in this sense: every program
you can write with parentheses is valid Koskari, and layout adds surface forms
that lower to the same underlying trees. Layout does not add evaluators or
special forms you cannot reach from s-expressions; it adds ways to **write** the
same pipelines.

---

## Three surfaces

The same conditional logic can appear in three authoring forms. They differ in
**source text** and often in **reader output** before macro expansion, even when
runtime behaviour matches.

| Surface | Source shape | Typical use |
| ------- | ------------ | ------------- |
| **Layout block** | Header + structural `:` + newline + indented body (optional peers) | `.ki` files, multi-line control flow |
| **Layout line** | One depth-0 line, no structural trailing `:`; multiple forms may fold | Compact top-level or body lines |
| **S-expression** | Parenthesized form | Nested expressions, explicit calls, REPL one-liners |

### Layout block

Each clause is its own block: header line, mandatory newline, indented body.
Peers such as `elif` and `else` are separate blocks at the same indent as the
first header.

```koskari
if test:
  then-form
else:
  else-form
```

Block `if` is handled by [layout/if](../layouts/layout-if.md) and lowers to
`compile/cond` at **read** time — before macro expansion.

### Layout line

A single line with several top-level forms folds to an implicit call shape
`(head arg …)` when implicit calls are enabled (the default):

```koskari
if test then-form else-form
```

Line `if` reads as `(if test then-form else-form)` and expands through the
[compile/if](../macros/compile-if.md) macro.

### S-expression

Explicit parentheses work at any depth:

```koskari
(if test then-form else-form)
```

Reader output matches the source shape; expansion follows the same macro path
as a layout line.

### Read shapes differ; eval can match

| Surface | Example source | Reader output (informal) |
| ------- | -------------- | ------------------------ |
| Layout block `if` | `if True:` / `1` / `else:` / `2` | `(compile/cond (True 1) (else: 2))` |
| Layout block `when` | `when True:` / `42` | `(when True 42)` |
| Layout line `if` | `if True 1 2` | `(if True 1 2)` |
| S-expression `if` | `(if True 1 2)` | `(if True 1 2)` |

Block `when` and `unless` use the default [layout/block](../layouts/layout-block-default.md)
handler (no dedicated layout plugin). Line and s-expression `when` read as
`(when cond body)` and expand through [compile/when](../macros/compile-when.md).

`elif` exists only as a **peer block** after block `if`; there is no layout-line
spelling for elif chains.

---

## Conditionals

### Block `if` / `elif` / `else`

```koskari
if (< x 0):
  "negative"
elif (equal? x 0):
  "zero"
else:
  "positive"
```

### Block `when` and `unless`

```koskari
when (> score 90):
  (print "excellent")

unless (nil? name):
  (print "hello" name)
```

### Layout line

```koskari
if True 1 2
when True 42
unless False 42
```

### S-expression

```koskari
(if True 1 2)
(when True 42)
(unless False 42)
```

---

## Where to go next

- [Reader and layout syntax](../reader-layout.md) — normative block, line, colon, and peer rules
- [Layout handler reference](../layouts/index.md) — per-head lowering (`layout/if`, …)
- [layout/if](../layouts/layout-if.md) — block conditional peers
- [compile/if](../macros/compile-if.md), [compile/when](../macros/compile-when.md), [compile/unless](../macros/compile-unless.md) — macro forms for line and s-expression surfaces
- [Layout and calls](../../start/from-python/layout-and-calls.md) — tutorial patterns in `.ki` files
