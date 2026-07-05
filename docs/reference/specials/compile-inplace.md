# compile/inplace
**Symbol:** `compile/inplace`

**Syntax:** `(inplace OP VAR VALUE)`

**Use-case:** In-place update of a **symbol** binding: compute `VAR OP VALUE` and
store the result back. Equivalent to Python augmented assignment on a name
(`x += 1`, `x &= mask`, etc.). OP is any numeric or bitwise operator; VAR must
be a symbol.

For item or attribute places, the **`compile/inplace`** macro (extended in
bootstrap) dispatches to [`compile/inplace-item`](compile-inplace-item.md) or
[`compile/inplace-attr`](compile-inplace-attr.md) instead.

**Behaviour:** Compiles VALUE, emits the corresponding VAR_OBJECT_INPLACE_* opcode, then
CONST None. The compiler may lower VAR_OBJECT_INPLACE_* to FAST_OBJECT_INPLACE_* when VAR has a
fixed slot.

**Supported operators:** `+`, `-`, `*`, `/`, `//`, `%`, `**`, `@`, `<<`, `>>`,
`&`, `^`, `|` (and their `compile/` forms, e.g. `compile/+`, `compile/-`, etc.).

**Examples:**

```koskari
;; Via set! macro
(set! x (+ x 1))    ; x += 1
(set! n (* n 2))    ; n *= 2
(set! bits (| bits mask))  ; bits |= mask

;; Direct use on a symbol
(inplace + counter 1)
(inplace // total divisor)

;; Item and attribute places (macro extension)
(inplace + (item vec 0) 1)
(inplace * (attr obj scale) 2)
```

**Implementation:** `koskari/compile/specials/object.py` — `special_inplace`
