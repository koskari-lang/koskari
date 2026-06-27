# Macro matching and hygiene

Compile-time macros transform source forms before the compiler sees the
result. Two ideas often confused under one word **match**: structural matching
of input forms, and unrelated runtime or control-flow “match”. **Datum** also
has two senses: a macro helper form and a hygiene wrapper around captured syntax.

## Match (macro and special transformers)

In reference docs, **match** usually means: given a source form, test it against
a **pattern** and run the matching arm’s template.

Mechanisms include:

- **`syntax-rules`** / **`macro-rules`** / **`special-rules`** — clause lists
- **`syntax-match`** / **`macro-match`** / **`special-match`** — named transformer
  specs (see [Macros index](../macros/index.md) and [Layouts index](../layouts/index.md))
- Compiler **`@special(..., patterns=...)`** arms — same matcher engine over
  spine subjects

The matcher is implemented in native code with patterns built in Python; API
detail lives in [`koskari.matcher`](../../api/koskari-matcher.md) and
[`koskari.hygiene`](../../api/koskari-hygiene.md).

This is **not** the same as:

- **`compile/switch`** — value dispatch on runtime data
- Exception matching at the VM layer
- A future runtime **`match`** / **`match-case`** value form (not documented here
  as shipped surface syntax)

When the [Glossary](../glossary.md) says **match**, it means macro/special
pattern matching unless context says otherwise.

## Datum (macro form)

Inside macro bodies, **`(datum expr)`** or **`(datum expr pos)`** captures an
expression together with **source position** for hygiene. It expands to a call
that records where the fragment came from.

- Macro: [`compile/datum`](../macros/compile-datum.md)
- Related: **`datumq`**, **`parse-match-patternq`**, **`parse-rule-patternq`**
  in bootstrap syntax helpers

Authors use **`datum`** so renamers and binder collection do not treat
substituted user fragments as freshly introduced identifiers.

## Datum (hygiene wrapper)

At expand time, captured pattern-variable values are often wrapped in a
**Datum** object (syntax box): a value plus optional position and flags. Tools
that walk expanded code treat those boxes as opaque for renaming when the capture
came from the input form, not from the template.

Macro expansion **finalize** steps peel `Datum` wrappers before compile. The
Python/C types are documented under
[`koskari.hygiene`](../../api/koskari-hygiene.md) and
[`koskari.lib.Datum`](../../api/koskari-lib.md).

| Sense | Role |
|-------|------|
| **`(datum …)` form** | Macro author syntax to attach position |
| **Datum wrapper** | Runtime/expansion representation of a captured fragment |

## Practical flow

```text
Source form
    → matcher selects arm (match)
    → template may use (datum …) for hygiene
    → expanded ordinary forms
    → compile as usual
```

## Related reading

- [Macros](../macros/index.md) — `define-syntax`, `extend-syntax`, rule forms
- [compile/let-syntax](../macros/compile-let-syntax.md) — local compile-time macro binding
- [compile/inline-macro](../macros/compile-inline-macro.md) — compile-time source splice
- [compile/datum](../macros/compile-datum.md)
- [Glossary: macro](../glossary.md), [Glossary: form](../glossary.md)
