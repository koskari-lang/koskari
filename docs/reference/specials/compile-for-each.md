# compile/for-each
**Symbol:** `compile/for-each`

**Syntax:** `(for-each [BINDING EXPR] BODY... [else: ELSEBODY...])`

**Use-case:** Iterate over the sequence produced by EXPR, binding each element
to BINDING and evaluating BODY on each iteration. Supports `compile/break` and
`compile/continue` inside the body. An optional trailing **`[else: ELSEBODY...]`**
keyword clause runs only when the iterator is exhausted naturally (i.e. not via
`compile/break`); its value becomes the loop's expression value. Without an
else clause the loop evaluates to `None`. In block syntax, the same clause comes
from an `else:` peer; see [layout/while](../layouts/layout-while.md) (shared
`layout_loop` handler for `for-each` and `while`).

**Behaviour:** Emits CONST None (default loop result), compiles EXPR (the
iterable), then PUSH_FOREACH whose break target is set to a label after
POP_FOREACH. The continue target (first instruction after PUSH_FOREACH) is
ITER_NEXT. On each iteration, ITER_NEXT pushes the next value or jumps to the
else label on exhaustion. The value is stored with VAR_STORE into the binding
name. The body is compiled with `helper_begin`; CONTINUE 0 resets the stack
block and jumps back to ITER_NEXT.

On exhaustion, CLEAR_FOREACH marks natural exit. When an else body is present, its
result is compiled and then BREAK 1 is emitted: this pops the FOREACH region,
pushes the else result directly to the parent stack, and jumps to after_pop —
so the else value arrives cleanly without any temporary variable. When there is
no else body, POP_FOREACH falls through to after_pop and pushes the region's None.
User `compile/break` also jumps to after_pop with its value already on the
parent stack.

**Examples:**

```koskari
;; Basic iteration; loop value is None
(for-each [i items]
  (append result i))

;; Else fires when iterator exhausts without a break
(for-each [i items]
  (cond
    [(> i 4) (break)]
    [else: (append result i)])
  [else: "finished"])

;; Break with a value
(for-each [i items]
  (cond
    [(== i target) (break "found")]
    [else: None]))
```


