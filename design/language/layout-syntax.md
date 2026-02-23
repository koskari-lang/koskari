# Koskari Syntax & Layout System (Revised Draft)

This document describes the **default Koskari syntax** and the layout-based reader model we have decided on.

Koskari now has a first-class, whitespace-friendly surface syntax that lowers into canonical s-expressions. Pure s-expr syntax remains fully supported and may be embedded anywhere.

The layout system is:

* Module-aware
* Extensible
* Data-driven
* Lowered deterministically into normal Koskari forms

The goal is: **the power of Lisp, with syntax that does not scare people off.**

---

# 1. High-Level Model

## 1.1 Default Reader

The default reader for a module is the **Koskari Layout Reader**.

It supports:

* Indentation-based block syntax
* Extended s-expr syntax
* Embedded pure s-expr regions
* Pluggable layout heads via `layout/<head>` objects

Pure s-expr syntax remains valid and can be used anywhere.

---

## 1.2 Two Phases of Layout

Layout objects participate in two phases:

1. **Read Phase** — structural parsing
2. **Expand Phase** — lowering to canonical forms

Each layout head (e.g. `layout/if`) may implement:

* `read(...) -> layout-form`
* `expand(layout-form, ctx) -> s-expr`

### Strict Invariant

* Read phase must only produce `layout/*` forms.
* Expand phase produces canonical s-exprs or `compile/*` forms.
* Reader never emits bytecode.

---

# 2. Block Syntax

## 2.1 Block Opener

A line ending in `:` starts a block.

Example:

```kosk
let [[x 1] [y 2]]:
  (+ x y)
```

### Block Detection Rule

In layout mode, a block header is any line where the **last significant token is `:`**.

The colon must be the final token on the line.

---

## 2.2 Keyword vs Block Colon

We distinguish two forms:

* `name:` → keyword token
* `name :` → symbol `name` followed by standalone `:` token

This allows:

```kosk
with [(foo) as: x]:
  ...
```

Here `as:` is a keyword.

Versus:

```kosk
assume x :
  ...
```

Here `x` is a symbol, and the standalone `:` opens the block.

Rule:

* A colon attached to a token forms a keyword.
* A colon separated by whitespace is a standalone token.
* Only a standalone `:` or a keyword-at-end-of-line may serve as a block opener.

---

# 3. layout/<head> Resolution

When the reader encounters a block header:

```
if expr:
```

It performs:

1. Resolve `layout/if` in module globals, then builtins.
2. If found, delegate read behavior to that layout object.
3. If not found, fall back to default block behavior.

Resolution happens at **read time**.

This makes layout behavior:

* Module-local
* Dynamically extensible
* Deterministic for already-read forms

---

# 4. Default Block Behavior

If no `layout/<head>` exists, the reader produces:

```lisp
(layout/block HEAD ARG1 ARG2 ... BODY...)
```

This is the generic fallback.

### Default Lowering

`layout/block` lowers mechanically to:

```lisp
(HEAD ARG1 ARG2 ... BODY)
```

Where BODY is:

* 0 forms → `None`
* 1 form → that form
* N > 1 forms → `(begin ...)`

This enables natural DSL usage:

```kosk
foobar [x y z]:
  (print x y z)
```

Lowers to:

```lisp
(foobar [x y z]
  (print x y z))
```

If `foobar` is a macro or syntax, it expands normally.

No layout plugin required.

---

# 5. Peering (else / except / etc.)

Layout objects are responsible for peer handling.

Example:

```kosk
if cond:
  a
else:
  b
```

Flow:

1. Reader resolves `layout/if`.
2. `layout/if.read(...)` reads its body.
3. It looks ahead at same indent for valid peer headers.
4. It consumes `else:` and attaches it as a peer.

Peering is owned entirely by the layout object.

The core reader does not implement global continuation rules.

---

# 6. layout/* Form Structure

Layout read hooks must return structured layout forms, e.g.:

```lisp
(layout/if <cond> <then-body> <clauses...>)
```

Peers may be represented as:

```lisp
[else: <body>]
```

Or other structured clause representations.

Expand hooks then lower these into canonical s-expr or `compile/*` forms.

---

# 7. Mixed-Mode Syntax

## 7.1 Entering Pure s-expr Mode

If a line begins with:

* `(`
* `[`
* `{`

The reader enters pure s-expr mode until the delimiter closes.

Indentation is ignored inside s-expr mode.

After the closing delimiter, layout mode resumes.

Example:

```kosk
if cond:
  (some (deep (normal s-expr)) here)
```

---

# 8. Module-Aware Reader

Each module has a `__reader__` object.

The reader:

* Resolves layout heads via module globals and builtins
* May be replaced entirely by module code
* Must ultimately return layout forms compatible with lowering

Reader changes only affect subsequent reads.

Already-read forms remain structurally fixed.

---

# 9. Fence Blocks (Planned Integration)

Fence blocks are part of the design.

Example:

````
```kosk:json
{ "a": 1 }
```
````

The fence resolves to a layout object such as:

```lisp
(layout/with-layout json <source>)
```

Which delegates to a sub-reader and returns layout forms.

Fence readers must lower to canonical Koskari forms.

---

# 10. Summary of Decisions

We have decided:

* Default syntax is layout-enabled.
* Pure s-expr is always allowed.
* Block heads resolve via `layout/<head>` lookup at read time.
* Layout objects own peering behavior.
* Fallback is `layout/block` → normal call lowering.
* Colon spacing distinguishes keyword vs block opener.
* Layout objects have separate read and expand phases.
* Reader is module-aware and replaceable.

This gives us:

* Python-like ergonomics for writing
* Lisp-level extensibility and macro power
* Module-local syntax evolution
* Deterministic lowering into canonical forms

The result is a coherent, extensible surface syntax that remains fundamentally Lisp at its core.
