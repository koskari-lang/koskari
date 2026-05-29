# compile/while

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/while`

**Syntax:** `(compile/while TEST BODY... [else: ELSEBODY])`

**Use-case:** Loop that runs body while TEST is truthy. Supports `compile/break`
and `compile/continue` inside the body. An optional `[else: ELSEBODY]` clause
runs only when the loop exits because TEST is false (i.e. not via
`compile/break`); its value becomes the loop's expression value. Without an
else clause the loop evaluates to `None`. In block syntax, the same clause comes
from an `else:` peer; see `layout/while` in [LAYOUTS.md](../../design/LAYOUTS.md).

**Behaviour:** Emits PUSH_LOOP (break target after POP_LOOP). The continue
target is the first instruction after PUSH_LOOP. Compiles TEST; on false,
jumps to the loop exit label. Compiles the body with `helper_begin`, then
CONTINUE 1 back to the test. On normal exit (test false), when an else body is
present, emits CLEAR_LOOP, compiles ELSEBODY, then BREAK 1 so the else result
reaches the parent stack before POP_LOOP. User `compile/break` jumps to the
break target with its value and does not run the else clause.

**Examples:**

```scheme
;; Simple loop
(compile/while x
  (compile/call append c x)
  (compile/setq x (compile/call sub x 1)))

;; With break
(compile/while True
  (compile/cond [done (compile/break result)]
                [else: (compile/call step)]))

;; Else runs when TEST becomes false
(compile/while x
  (compile/setq x (compile/call sub x 1))
  [else: "done"])
```

**Tests:** `tests/compile/specials/test_while.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
