# compile/for-each

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/for-each`

**Syntax:** `(compile/for-each [BINDING EXPR] BODY... [else: ELSEBODY])`

**Use-case:** Iterate over the sequence produced by EXPR, binding each element
to BINDING and evaluating BODY on each iteration. Supports `compile/break` and
`compile/continue` inside the body. An optional trailing **`[else: ELSEBODY]`**
keyword clause runs only when the iterator is exhausted naturally (i.e. not via
`compile/break`); its value becomes the loop's expression value. Without an
else clause the loop evaluates to `None`. In block syntax, the same clause comes
from an `else:` peer; see `layout/for-each` in [LAYOUTS.md](../../design/LAYOUTS.md).

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

```scheme
;; Basic iteration; loop value is None
(compile/for-each [i items]
  (compile/call append result i))

;; Else fires when iterator exhausts without a break
(compile/for-each [i items]
  (compile/cond
    [(compile/call gt i 4) (compile/break)]
    [else: (compile/call append result i)])
  [else: "finished"])

;; Break with a value
(compile/for-each [i items]
  (compile/cond
    [(compile/call eq i target) (compile/break "found")]
    [else: None]))
```

**Tests:** `tests/compile/specials/test_for_each.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
