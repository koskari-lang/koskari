# Koskari Macros — Implemented Reference

This document describes the macros currently implemented in Koskari (via
`compile/define-syntax` and syntax-rules). Macros expand at compile time before
the compiler sees the result; the expanded forms are then compiled as normal.
For compiler special forms, see [SPECIALS.md](SPECIALS.md).

Macros documented here are defined in `koskari/importer/basics.ki` and are
available in every module without import. The format mirrors SPECIALS.md to
ease future migration of purely transformative forms from specials to macros.


## set!

**Symbol:** `set!` (defined as `compile/set!`)

**Syntax:**
- Plain assignment: `(set! SYM EXPR)`
- Augmented assignment: `(set! SYM (OP SYM EXPR))` where OP is `+`, `-`, `*`,
  `/`, `//`, `%`, `**`, `@`, `<<`, `>>`, `&`, `^`, or `|`
- Item assignment: `(set! (item OBJ KEY) VAL)`
- Attribute assignment: `(set! (attr OBJ NAME) VAL)`
- Ref assignment: `(set! (deref REF) VAL)`

**Use-case:** User-facing assignment syntax. Covers variable store, augmented
assignment, subscript store, attribute store, and mutable reference store.

**Behaviour:** Expands via syntax-rules. Plain assignment expands to
`(compile/setq SYM EXPR)`. Augmented forms like `(set! x (+ x 1))` expand to
`(compile/inplace OP SYM EXPR)`. Item, attribute, and deref forms expand to
`(set-item ...)`, `(set-attr ...)`, and `(set-ref ...)` respectively. The first
match in the syntax-rules wins.

**Examples:**

```scheme
;; Plain assignment
(set! x 42)
(set! counter (add counter 1))

;; Augmented assignment
(set! x (+ x 1))      ; x += 1
(set! n (* n 2))      ; n *= 2
(set! bits (| bits mask))

;; Item and attribute
(set! (item vec 0) 99)
(set! (attr obj count) 0)

;; Mutable reference
(set! (deref r) 100)
```

**Defined in:** `koskari/importer/basics.ki`


## ref

**Symbol:** `ref` (defined as `compile/ref`)

**Syntax:**
- Variable reference: `(ref SYM)`
- Attribute reference: `(ref (attr OBJ NAME))` or `(ref (compile/attr OBJ NAME))`
- Item reference: `(ref (item OBJ KEY))` or `(ref (compile/item OBJ KEY))`

**Use-case:** User-facing syntax for obtaining a mutable reference. The `&expr`
reader macro expands to `(compile/ref expr)`.

**Behaviour:** Expands via syntax-rules. Plain symbol expands to
`(compile/refq SYM)`. `(attr OBJ NAME)` and `(compile/attr OBJ NAME)` expand to
`(compile/ref-attr OBJ NAME)`. `(item OBJ KEY)` and `(compile/item OBJ KEY)`
expand to `(compile/ref-item OBJ KEY)`.

**Examples:**

```scheme
(ref x)                    ; → (compile/refq x)
(ref (attr obj count))     ; → (compile/ref-attr obj count)
(ref (item lst 0))         ; → (compile/ref-item lst 0)
&foo                       ; reader: (compile/ref foo) → refq
&foo.bar                   ; reader: (compile/ref (attr foo bar)) → ref-attr
```

**Defined in:** `koskari/importer/basics.ki`

