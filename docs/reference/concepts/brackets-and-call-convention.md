# Brackets and the call convention

Koskari reads `( )` and `[ ]` with the **same grammar**. A `[ ]` form is **not**
a vector, array, or "literal list" that behaves differently from `( )`. Both
read into ordinary pair chains, and **by default both compile the same way**.
This page explains the convention that gives `[ ]` its meaning, and how to turn
that convention into an enforced check.

## Same grammar, same default compilation

A non-empty list in head (operator) position compiles to a **call** — the first
element is applied to the rest — no matter which bracket delimited it:

```koskari
(f 1 2)   ; call f with 1 and 2
[f 1 2]   ; identical: call f with 1 and 2
```

So `[a 1]` is a call to `a` with argument `1`, exactly like `(a 1)`. If you are
coming from a language where `[...]` builds a list value, this is the single
most important thing to unlearn. To build collection values, Koskari uses the
`#(...)`, `#[...]`, and `#{...}` reader forms (tuple, list, dict), not bare
brackets.

## What the brackets actually carry

The reader records that a form came from `[ ]` by producing a **bracket-pair**
(and `bracket-nil` for the empty `[]`). A bracket-pair is value-equal to and
fully interchangeable with a normal pair — every pair operation works on it —
it simply remembers its bracket origin. That memory does nothing on its own; it
exists so tooling and an opt-in compiler check can use it.

## The convention

The convention — and by default it is **only** a convention — is:

- **`( )` means a call/apply form.** The first element is the thing being
  applied; the rest are its arguments. Some forms evaluate every argument;
  others deliberately treat certain arguments as data.
- **`[ ]` means a non-call data fragment.** Binding lists, `cond` clauses,
  pattern rows — structure that the enclosing form consumes as data, never
  applies as a call.

This is first of all a **reading aid**. Nobody expects

```koskari
(let [[a 1]] (+ a 100))
```

to *call* `a` with `1` — the brackets make "this is a binding, not a call"
visible at a glance. Binding lists like this are consumed as data by `let`, so
they never reach the call compiler regardless of the convention.

## Why it can also catch bugs

Because brackets mark intended-data positions, a bracketed fragment that
*accidentally* ends up in call position is a strong signal of a mistake. Take a
typo — `ler` instead of `let`:

```koskari
(ler [[a 1]] (+ a 100))
```

`ler` is not a special form, so this compiles as a **call** to the value `ler`,
whose first argument is `[[a 1]]` — i.e. `(call (call a 1))`. That is clearly
not what the author meant. The bracket marker lets the compiler notice that a
`[ ]` fragment reached call position and tell you.

## Turning the convention into a check

Two independent, **per-module** settings control this, set through the
[pragma](pragma.md):

| Setting | Affects | Values | Meaning |
|---|---|---|---|
| `reader/brackets` | reader | on / off | Whether `[ ]` reads into bracket-pairs (carrying the marker) or plain pairs. Default **on**. |
| `compiler/call-brackets` | compiler | `ignore` / `warn` / `error` | What happens when a bracket-pair reaches call position. Default **`ignore`** (pure convention). |

Each setting is a bracketed data fragment:

```koskari
; make accidental "[ ] in call position" a hard error for this module
(pragma [compiler/call-brackets error])

; or just warn
(pragma [compiler/call-brackets warn])
```

A pragma is forward-only and scoped to the module that sets it; it does not
affect modules you import. With `compiler/call-brackets` at `ignore` (the
default) the language is exactly the convention — brackets are documentation.
Raising it to `warn` or `error` turns the convention into a diagnostic that
catches the `ler` class of mistake at compile time.

## See also

- [Layout and s-expressions](layout-and-s-expressions.md) — the two surfaces
  brackets appear in.
- [Grammar](../grammar.md) — the formal reader grammar for lists and brackets.
