# compile/quasiquote
**Symbol:** `compile/quasiquote`

**Syntax:** ``EXPR` (reader shorthand) or `(quasiquote EXPR)`

**Use-case:** Quasi-quotation: construct a pair structure at compile time with
selected sub-expressions evaluated at runtime. Inside the body, `,EXPR`
(unquote) splices a single evaluated value, and `,@EXPR` (unquote-splicing)
flattens a list into the surrounding list.

**Reader shorthands:**

- ``EXPR` → `(quasiquote EXPR)`
- `,EXPR` → `(unquote EXPR)` (only valid inside a quasiquote body)
- `,@EXPR` → `(unquote-splicing EXPR)` (only valid inside a quasiquote body)
- ``HEAD: BODY` → `(quasiquote-block (:block: ...))` (block quasiquote; see below)

**Behaviour:** One argument required. The compiler walks the expression
recursively at a nesting *level* (starting at 0):

- **Atoms and constants** (symbols, keywords, numbers, nil): emitted as `CONST`.
- `**(unquote INNER)`** at level 0: `INNER` is compiled normally
(evaluated at runtime); its result becomes that position's value.
- `**(unquote INNER)**` at level > 0: the unquote form is reconstructed
literally as a pair — `CONST compile/unquote`, recurse on INNER at level − 1,
`PROPER 2`.
- `**(quasiquote INNER)**` at any level: reconstructed literally at
level + 1.
- **Proper lists**: walked element by element. Consecutive non-splice elements
are accumulated into *segments*:
  - If no `unquote-splicing` site is present anywhere in the list, each segment
  is emitted individually with `PROPER n`.
  - If any splice is present, constant-only segments are folded into a single
  `CONST (e0, e1, ...)` tuple (which `PAIR_SPLICE` accepts natively); mixed
  segments use `PROPER n`. Each splice site compiles its inner expression
  directly (must produce an iterable pair or nil). All segments and splices are
  gathered then combined with `PAIR_SPLICE count`.
- **Dotted (improper) pairs**: like a proper list but the non-nil tail is emitted
after all elements and the whole thing is closed with `CONS count+1` (the tail
is explicit). Splice in dotted-tail position raises `SpecialError`.

**Nesting:** Each additional ``` increments the level; each `,` decrements
it. At level > 0, unquote forms are data, not escapes.

**Note on dotted tails with unquote:** `(a . ,x)` is parsed by the reader as the
three-element proper list `(a compile/unquote x)` (because `(a . (unquote x))`
collapses to a proper list). Only atomic tails produce genuine improper pairs
(e.g. ``(a . 42)` or ``(a . b)`).

**Opcodes emitted:** `CONST`, `PROPER`, `CONS`, `PAIR_SPLICE` (see OPCODES.md §5.1).

**Examples:**

```koskari
;; Atom: just a constant
`42           ; → 42
`foo          ; → symbol foo

;; Proper list — compiles to PROPER 3
`(a b c)      ; → (a b c)

;; Unquote — x is evaluated at runtime
`(a ,x b)     ; → (a <value of x> b)

;; Unquote-splicing — items is spliced in
`(a ,@items b) ; → (a item0 item1 ... b)

;; Dotted pair with atom tail
`(a . 42)     ; → (a . 42)

;; Nested quasiquote — inner unquote is NOT evaluated at outer level
``(a ,(+ 1 2)) ; → (quasiquote (a (unquote (+ 1 2))))
```

**Implementation:** `koskari/compile/specials/quasiquote.py`


