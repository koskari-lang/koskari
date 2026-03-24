# Koskari Syntax & Layout System (Revised Draft)

This document describes the **default Koskari syntax** and the layout-based reader model we have decided on.

**Formal syntax:** The authoritative grammar is defined in [layout-syntax-formal.md](layout-syntax-formal.md) (EBNF). This document covers the design model; the formal spec covers lexical preprocessing, logical lines, delimiter depth, and the grammar.

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

The formal grammar (see [layout-syntax-formal.md](layout-syntax-formal.md)) defines:

* **Logical lines** — a physical newline ends a logical line only when delimiter depth is 0
* **Header terminator** — the final `:` at delimiter depth 0, after which only whitespace/comment remain; it is not part of header content
* **Multiline continuation** — newlines inside balanced `()`, `[]`, `{}` do not end the header

## 2.1 Block Opener

A block header is terminated by a trailing `:` as above.

Example:

```kosk
let [[x 1] [y 2]]:
  (+ x y)
```

Headers may span multiple physical lines when delimiters are unbalanced, e.g. `let [[a 1]\n     [b 2]]:`.

## 2.2 Header Terminator: Colon Handling

Both `unknown x:` and `unknown x :` parse identically. The reader treats the **final** `:` at delimiter depth 0 as the block opener; spacing around it does not matter.

Keywords like `as:`, `x:`, `y:` remain intact inside headers. Only the colon that terminates the header is structural. Example:

```kosk
foo x: 1 y: 2:
  (+ x y)
```

Here `x:` and `y:` are keywords in the header; the last `:` ends the header. Similarly:

```kosk
with [(foo) as: x]:
  ...
```

Here `as:` is a keyword; the trailing `:` opens the block.

### Implementation: Single-Pass Header Parsing

The reader uses a single forward pass. With `:` installed as a terminating event
macro at delimiter depth 0, the first colon encountered at the top level fires
the COLON event and ends the header. No probe or replay is needed. This ensures
`x:` and `x :` behave the same and that keywords elsewhere in the header are
preserved.

## 2.3 Block Parsing Flow and Source Structure

**read_block** = layout resolution over an anonymous block read.

**Anonymous block read** produces `(header . body)`:

* Read header forms (head + args) up to the terminating `:`.
* Read body elements until dedent. Each element is either a simple form or a sub-block.
* When a sub-block is detected (same indent, header ending in `:`), recur into **read_block** (full layout path) and append its result to the body.
* Inner blocks are thus processed by layouts first; their results become body elements for the outer block.

**Source shape:** Header and body are a pair, not a single joined form:

```lisp
((if x) . ('true . nil))
((else . nil) . ('false . nil))
```

For a block `if cond:\n  a`:

* `header` = `(if . (cond . nil))`
* `body` = `(a . nil)`
* `source` = `(header . body)`

For peer blocks (e.g. `else:`), each peer is `(pheader . pbody)`.

**Default fallback:** If no `layout/<head>` is found, the reader prepends `layout/block` to the header and invokes the `layout/block` handler with the synthetic source. That handler lowers to `(HEAD ARG1 ARG2 ... BODY)`.

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

If no `layout/<head>` exists, the reader builds a synthetic source by prepending `layout/block` to the header, then invokes the `layout/block` handler:

```lisp
;; synthetic_header = (layout/block HEAD ARG1 ARG2 ...)
;; default_source = (synthetic_header . body)
```

The `layout/block` handler receives this source and lowers it to:

```lisp
(HEAD ARG1 ARG2 ... BODY)
```

Where BODY is:

* 0 forms → `None`
* 1 form → that form
* N > 1 forms → `(begin ...)` (or equivalent)

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

Layout objects declare peer heads (e.g. `elif`, `else` for `layout/if`). The reader gathers contiguously following blocks at the same indent whose heads match the declared set, builds `(pheader . pbody)` pairs for each, and passes them as the `peers` argument to the layout's `read` hook.

Example:

```kosk
if cond:
  a
else:
  b
```

Flow:

1. Reader resolves `layout/if`.
2. Reader reads the if block body (including any sub-blocks via `read_block`).
3. Reader calls `_read_declared_peers` with the layout's declared peers (`elif`, `else`).
4. For each matching peer at same indent, the reader consumes the block and appends `(pheader . pbody)` to peers.
5. `layout/if.read(reader, source, peers)` receives the source and peer list.

Peering is driven by layout metadata; the core reader implements the gathering loop but does not assign meaning to particular heads.

---

# 6. layout/* Form Structure

Layout read hooks receive:

* **source** — `(header . body)` where `header` = `(head . args)` (variadic cons), `body` = list of body forms (possibly including sub-block results).
* **peers** — list of `(pheader . pbody)` pairs for declared peer blocks.

Handlers return structured layout forms, e.g.:

```lisp
(layout/if <cond> <then-body> <clauses...>)
```

The layout object is responsible for interpreting peers (e.g. `else` → `(else . body)` or `(keyword('else) . body)`).

Expand hooks then lower these into canonical s-expr or `compile/*` forms.

---

# 7. Mixed-Mode Syntax

## 7.1 Entering Pure s-expr Mode

When a `HeaderPart` or `ExprPart` is scanned and the next character is `(`, `[`, or `{`, the s-expr reader parses a complete balanced expression and yields it as a single `SEXPR` terminal. See the formal spec §7.1.

Indentation inside `SEXPR` is ignored by the layout grammar.

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
* Layout objects declare peers; the reader gathers them and passes `(pheader . pbody)` pairs.
* Fallback is `layout/block` → normal call lowering.
* The final `:` at depth 0 terminates the header; `x:` and `x :` parse identically; keywords elsewhere stay intact.
* Block source is `(header . body)`; layout handlers receive source and peers.
* Layout objects have separate read and expand phases.
* Reader is module-aware and replaceable.

This gives us:

* Python-like ergonomics for writing
* Lisp-level extensibility and macro power
* Module-local syntax evolution
* Deterministic lowering into canonical forms

The result is a coherent, extensible surface syntax that remains fundamentally Lisp at its core.
