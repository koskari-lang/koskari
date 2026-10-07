# compile/while
**Symbol:** `compile/while`

**Syntax:** `(while TEST BODY... [else: ELSEBODY...])`

**Use-case:** Loop that runs body while TEST is truthy. Supports `compile/break`
and `compile/continue` inside the body. An optional `[else: ELSEBODY...]` clause
runs only when the loop exits because TEST is false (i.e. not via
`compile/break`); its value becomes the loop's expression value. Without an
else clause the loop's value is the value of its last iteration: the body's
last form, or the value given to `compile/continue`; `None` when the body
never ran. `(break VAL)` makes VAL the loop's value. In block syntax, the
same clause comes from an `else:` peer; see
[layout/while](../layouts/layout-while.md).

**Behaviour:** Emits PUSH_LOOP (break target after POP_LOOP). The continue
target is the first instruction after PUSH_LOOP. Compiles TEST; on false,
jumps to the loop exit label. Compiles the body with `helper_begin`, then
CONTINUE 1 back to the test. On normal exit (test false), when an else body is
present, emits CLEAR_LOOP, compiles ELSEBODY, then POP_LOOP, which delivers
the else result from the region's stack. The region is disabled while the
else body runs, so a `compile/break` or `compile/continue` written there
belongs to the enclosing loop. User `compile/break` from the body jumps to
the break target with its value and does not run the else clause.

**Examples:**

```koskari
;; Simple loop
(while x
  (append c x)
  (setq x (- x 1)))

;; With break
(while True
  (cond [done (break result)]
                [else: (step)]))

;; Else runs when TEST becomes false
(while x
  (setq x (- x 1))
  [else: "done"])
```
