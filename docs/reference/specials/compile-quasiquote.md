# compile/quasiquote
**Symbol:** `compile/quasiquote`

**Syntax:** ``EXPR` (reader shorthand) or `(quasiquote EXPR)`

**Use-case:** Quasi-quotation: construct a pair structure at compile time with
selected sub-expressions evaluated at runtime. Inside the body, `,EXPR`
(unquote) splices a single evaluated value, and `,@EXPR` (unquote-splicing)
flattens a list into the surrounding list.

**Reader shorthands:**

- ``EXPR` -> `(quasiquote EXPR)`
- `,EXPR` -> `(unquote EXPR)` (only valid inside a quasiquote body)
- `,@EXPR` -> `(unquote-splicing EXPR)` (only valid inside a quasiquote body)
- `` `HEAD: BODY `` -> `(quasiquote (#block: (HEAD) ...))`; see
  [Block quasiquote](compile-quasiquote-block.md)

**Behaviour:** One argument required. The result is the argument rebuilt as
data, with each escape replaced by a runtime value. The walk tracks a nesting
*level*, starting at 0:

- **Atoms** (symbols, keywords, numbers, nil) are themselves. A symbol a macro
  template wrote loses its hygiene mark; `quasiquote-syntax` keeps it.
- `(unquote INNER)` at level 0 evaluates `INNER` at runtime and puts its value
  in that position.
- `(unquote-splicing INNER)` at level 0, in list position, evaluates `INNER`
  at runtime and splices its elements into the surrounding list. The value
  must be a pair or nil. A splice in dotted-tail position is a compile-time
  error. A splice before a dotted tail ends the spliced list in that tail:
  `` `(a ,@xs . b) `` with `xs` bound to `(1 2)` gives `(a 1 2 . b)`, and
  `(a . b)` when `xs` is empty.
- `(unquote INNER)` and `(unquote-splicing INNER)` at level > 0 are data, not
  escapes: the form is rebuilt with `INNER` walked at level - 1.
- `(quasiquote INNER)` at any level is rebuilt as data with `INNER` walked at
  level + 1.
- **Proper lists** are rebuilt element by element; **dotted pairs** keep their
  tail. Positions that hold no escape are built as constants.

**Nesting:** Each additional ``` increments the level; each `,` decrements it.

**Dotted tails with unquote:** `(a . ,x)` reads as the three-element proper
list `(a unquote x)`, because `(a . (unquote x))` is a proper list. The
compiler treats that trailing `unquote` as a dotted tail and evaluates it, so
``(a . ,(car xs))` with `xs` bound to `(1 2)` gives `(a . 1)`. An atomic tail
also produces an improper pair, as in ``(a . 42)` or ``(a . b)`.

**Examples:**

```koskari
;; Atom: just a constant
`42           ; -> 42
`foo          ; -> symbol foo

;; Proper list
`(a b c)      ; -> (a b c)

;; Unquote -- x is evaluated at runtime
`(a ,x b)     ; -> (a <value of x> b)

;; Unquote-splicing -- items is spliced in
`(a ,@items b) ; -> (a item0 item1 ... b)

;; Dotted pair with atom tail
`(a . 42)     ; -> (a . 42)

;; Nested quasiquote -- inner unquote is NOT evaluated at outer level
``(a ,(+ 1 2)) ; -> (quasiquote (a (unquote (+ 1 2))))
```

**See also:** [`quasiquote-syntax`](compile-quasiquote-syntax.md),
[`quote`](compile-quote.md), [Hygiene](../../guide/hygiene.md).
