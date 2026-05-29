# compile/inplace
**Symbol:** `compile/inplace`

**Syntax:** `(inplace OP VAR VALUE)`

**Use-case:** In-place update: compute `VAR OP VALUE` and store the result back
into VAR. Equivalent to Python augmented assignment (`x += 1`, `x &= mask`, etc.).
OP is any numeric or bitwise operator; VAR must be a symbol.

**Behaviour:** Compiles VALUE, emits the corresponding VAR_OBJECT_* opcode (arg =
name constant index), then CONST None. The compiler may lower VAR_OBJECT_* to
FAST_OBJECT_* when VAR has a fixed slot.

**Supported operators:** `+`, `-`, `*`, `/`, `//`, `%`, `**`, `@`, `<<`, `>>`,
`&`, `^`, `|` (and their `compile/` forms, e.g. `compile/+`, `compile/-`, etc.).

**Examples:**

```scheme
;; Via set! macro (see MACROS.md)
(set! x (+ x 1))    ; x += 1
(set! n (* n 2))    ; n *= 2
(set! bits (| bits mask))  ; bits |= mask

;; Direct use
(inplace + counter 1)
(inplace // total divisor)
```

**Implementation:** `koskari/compile/specials/object.py` — `special_inplace`

