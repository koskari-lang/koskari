# compile/for-each
**Symbol:** `compile/for-each`

**Syntax:** `(for-each [BINDING EXPR] BODY... [else: ELSEBODY...])`

**Use-case:** Iterate over the sequence produced by EXPR, binding each element
to BINDING and evaluating BODY on each iteration. Supports `compile/break` and
`compile/continue` inside the body. An optional trailing **`[else: ELSEBODY...]`**
keyword clause runs only when the iterator is exhausted naturally (i.e. not via
`compile/break`); its value becomes the loop's expression value. Without an
else clause the loop's value is the value of its last iteration: the body's
last form, or the value given to `compile/continue`; `None` when the
sequence was empty. `(break VAL)` makes VAL the loop's value. In block
syntax, the same clause comes from an `else:` peer; see
[layout/while](../layouts/layout-while.md) (shared `layout_loop` handler for
`for-each` and `while`).

**Behaviour:** Emits CONST None (default loop result), compiles EXPR (the
iterable), then PUSH_FOREACH whose break target is set to a label after
POP_FOREACH. The continue target (first instruction after PUSH_FOREACH) is
ITER_NEXT. On each iteration, ITER_NEXT pushes the next value or jumps to the
else label on exhaustion. The value is stored with VAR_STORE into the binding
name. The body is compiled with `helper_begin`; CONTINUE 0 resets the stack
block and jumps back to ITER_NEXT.

On exhaustion, CLEAR_FOREACH marks natural exit and disables the region.
When an else body is present, it is compiled next and POP_FOREACH delivers
its value from the region's stack; a `compile/break` or `compile/continue`
written in the else body belongs to the enclosing loop, since this one has
finished. When there is no else body, POP_FOREACH pushes the region's result
(None unless a `compile/continue` set one). User `compile/break` from the
body jumps to after_pop with its value already on the parent stack.

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
