# Fences

What fenced foreign literals are, and when a format earns one.

---

## Introduction

A **fence** is a triple-backtick block that captures raw source in some
foreign format and lowers it, at compile time, into ordinary Koskari source:

````text
```bash
echo hello
```
````

The fence names a format (`bash`) on the opener line; the body is captured
verbatim. At read time the reader does not understand the body — it only
wraps it into a `(apply-fence NAME RAW META)` form. Compilation then
resolves a handler named `fence/<name>` and asks it to turn the raw text into
Koskari source, which is compiled in place of the fence.

So a fence handler is a **compile-time transliterator**, not a runtime parser.
This is the same shape as layout expansion (`:block:` conses lowered through
`apply_layout`): the reader captures structure, and a compile-time step turns
it into ordinary source before the rest of compilation runs.

For the dispatch mechanism see
[`compile/apply-fence`](../reference/specials/compile-apply-fence.md); for the built-in
handler and how it is implemented see the
[Fence reference](../reference/fences/index.md).

---

## When a format earns a fence

Fences are deliberately narrow. The bar for adding one is that the notation
must be **genuinely awkward to express in native Koskari** — not merely
different-looking.

The built-in `fence/bash` clears that bar: "capture a shell script, run it
asynchronously, and hand back a result object" is clumsy to write out as
s-expressions, reads cleanly at the call site, and slots into process-driven
work like a job scheduler.

Data notations mostly do **not** clear it. A JSON fence was prototyped and
dropped: `{ "a": 1 }` is barely different from the native `#{"a" 1}`, and
native literal forms give you compile-time structure a foreign parser can't.
When native syntax already expresses the value cleanly, a fence only adds a
second grammar to learn. Reach for a fence when the *foreign* form is the
clear one — a large template body, a shell script — not when it merely
resembles something you already have.

---

## Turning fences off

Fence reading is governed by the `reader/fences` pragma (baseline on). A
module that wants backtick to keep only its quote/quasiquote readings can
disable it:

```koskari
(pragma [reader/fences False])
```

See [Pragmas and feature settings](pragma.md).
