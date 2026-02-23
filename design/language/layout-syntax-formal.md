# Koskari Layout Syntax — Formal Specification (EBNF)

This document is the **formal declaration of the Koskari Layout Syntax** (the default Koskari reader mode).

It specifies:

* The indentation/block overlay
* The "final `:` terminates a header" rule
* Multiline continuation only inside balanced delimiters
* How pure s-expressions are embedded

It does **not** specify semantics of particular forms (`if`, `else`, `try`, etc.). Those are handled by **layout head objects** (`layout/<head>`), which may consume peer blocks and expand to canonical s-expressions.

---

# 1. Scope and Goals

The layout reader produces **layout forms** (typically cons trees), which are later lowered/expanded into canonical Koskari s-expressions.

Core goals:

* Block syntax is structural and deterministic.
* Only the **final** `:` at end-of-header opens a block.
* Newlines may occur inside delimited expressions without ending a header or expression.
* Pure s-expressions are always embeddable and are parsed by the s-expr reader.

---

# 2. Lexical Preprocessing

This specification assumes a preprocessing layer that produces:

* `INDENT` / `DEDENT` tokens (Python-style indentation)
* `NL` tokens for logical line breaks
* Comment elision (or explicit `COMMENT` tokens)

The syntax grammar operates on **logical lines**, not raw physical lines.

---

# 3. Logical Lines

## 3.1 Delimiter Depth

Let delimiter characters be:

* `OPEN` ∈ { `(`, `[`, `{` }
* `CLOSE` ∈ { `)`, `]`, `}` }

A **delimiter depth** counter is maintained while scanning.

* Encountering `OPEN` increases depth by 1.
* Encountering `CLOSE` decreases depth by 1.
* Depth must never be negative.

Strings/comments are assumed to be lexically handled such that delimiter characters inside them do not affect depth.

## 3.2 Logical Line Break Rule

A physical newline ends a **logical line** iff delimiter depth is 0.

If delimiter depth > 0, a physical newline is treated as whitespace and does not end the logical line.

This is the sole multiline continuation mechanism.

---

# 4. Header Terminator

A block header is terminated by a trailing `:` satisfying:

* The `:` occurs at delimiter depth 0.
* After the `:` there are only whitespace and/or a comment until the logical line ends.

The terminating `:` is **not part of header content**.

This implies:

* `if x:` is a valid header with content `if x`
* `let [[a 1]\n     [b 2]]:` is a single header (newline inside `[...]`)

---

# 5. Terminals and Token Classes

The following terminals are used by the grammar.

* `ATOM` — a single atomic token produced by the base lexer (symbol, number, string, keyword token, etc.)
* `SEXPR` — a balanced delimited s-expression parsed by the s-expr reader as a single unit
* `:` — the header terminator (structural)
* `NL` — a logical line break token
* `INDENT`, `DEDENT` — indentation boundary tokens

Notes:

* `SEXPR` may span multiple physical lines according to the delimiter depth rule.
* `ATOM` tokenization may include keyword-like atoms (e.g. `as:`), but the final header terminator `:` is structural and stripped.

---

# 6. Grammar (EBNF)

## 6.1 Module

```ebnf
Module        ::= Suite EOF ;
```

## 6.2 Suites and Lines

A `Suite` is a sequence of lines at a single indentation level.

```ebnf
Suite         ::= { Line }* ;

Line          ::= BlankLine
               |  CommentLine
               |  BlockLine
               |  ExprLine ;

BlankLine     ::= NL ;
CommentLine   ::= NL ;  (* comment is assumed removed or treated as whitespace *)
```

## 6.3 Blocks

A `BlockLine` is a `Header` followed by a structural terminator `:` and an indented nested suite.

```ebnf
BlockLine     ::= Header ':' NL INDENT Suite DEDENT ;
```

## 6.4 Headers

A `Header` is a non-empty sequence of `HeaderPart`.

```ebnf
Header        ::= HeaderPart { HeaderPart } ;
HeaderPart    ::= ATOM | SEXPR ;
```

## 6.5 Expressions

An expression line is either a single s-expression, or a "layout expression" consisting of one or more parts.

```ebnf
ExprLine      ::= Expr NL ;

Expr          ::= SEXPR
               |  LayoutExpr ;

LayoutExpr    ::= ExprPart { ExprPart } ;
ExprPart      ::= ATOM | SEXPR ;
```

---

# 7. Operational Notes (Non-Grammatical Requirements)

The following rules are required for a complete implementation but are intentionally **not encoded in the EBNF**, because they describe runtime reader delegation rather than syntax shape.

## 7.1 Embedded s-expression Mode

When scanning a `HeaderPart` or `ExprPart`, if the next character begins a delimited form (`(`, `[`, `{`), the s-expr reader parses a complete balanced expression and yields it as a single `SEXPR` terminal.

Indentation inside `SEXPR` is ignored by the layout grammar.

## 7.2 Layout Head Resolution

Given a `BlockLine` with header parts `H0 H1 ...`, where `H0` is an `ATOM` that is a symbol:

* The reader may resolve `layout/<H0>` in the module's globals and builtins.
* If present, it may delegate reading/peering behavior to that layout head object.
* If absent, it must fall back to default block behavior.

This affects the produced **layout forms**, but does not change the surface syntax grammar.

## 7.3 Peer Blocks

Peer blocks (e.g. `else:` following an `if:`) are syntactically just `BlockLine` nodes.

Whether a block is accepted as a peer is defined solely by the active layout head's read hook.

The base layout grammar does not assign special meaning to particular header symbols.

---

# 8. Examples (Conforming Syntax)

## 8.1 Simple block

```kosk
let [[x 1] [y 2]]:
  (+ x y)
```

## 8.2 Multiline header inside delimiters

```kosk
let [[a 1000]
     [b 2000]]:
  (+ a b)
```

## 8.3 Peer-shaped blocks (semantics owned by layout head)

```kosk
if (& a 1):
  'foo
else:
  'bar
```

---

# 9. Future Extensions (Out of Scope)

This document does not specify:

* Fence blocks (`` ```kosk:json ... ``` ``)
* The set of standard layout heads (`layout/if`, `layout/let`, etc.)
* Expansion/lowering contracts for layout heads

These will be specified in separate documents.
