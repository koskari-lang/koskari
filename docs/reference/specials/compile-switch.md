# compile/switch
**Symbol:** `compile/switch`

**Syntax:** `(switch VALUE [COMPARE-LIST BODY...] ... [else: BODY...])`

**Use-case:** Multi-way equality branching against one discriminant value.
`VALUE` is evaluated once, then each clause compares one or more candidate values
from `COMPARE-LIST` against `VALUE` in order. On first match, that clause body
is evaluated and returned. If no clause matches, `else:` is used when present;
otherwise the form yields `None`.

**Behaviour:** Requires at least one argument (`VALUE`). Compiles `VALUE` once
and keeps it on the stack across case checks. For each non-else clause, the
clause test must be a pair/list (`COMPARE-LIST`); otherwise `SpecialError` is
raised (`"switch compare must be a list of values"`). For each element of that
list, the compiler emits one compare path against `VALUE`:

- match (`SWITCH_EQ` not taken): emits `JUMP body_label`, then runs the clause
  body once
- no match (`SWITCH_EQ` taken): pops only candidate value, keeps `VALUE`, jumps
  to the next candidate (or next clause after the last candidate)

Clause bodies are compiled via `helper_begin`. The `else:` clause must be last.
Entering `else:` pops the retained `VALUE` before compiling its body. After any
successful clause, control jumps to a shared done label. If no clause matches
and no else is present, the remaining `VALUE` is popped and `None` is pushed.

The head may be written as `switch` or `compile/switch`; compiler lookup resolves
`switch` to `compile/switch`.

**Expected lowered shape from layout/switch:**

```koskari
(switch VALUE
  ((COND1 COND2 ...) (begin BODY...))
  (else: (begin BODY...)))
```

This shape is valid because each clause body is still a list of forms for
`helper_begin`; when it contains one `compile/begin` form, that form performs
the sequencing.

**Examples:**

```koskari
;; Direct s-expression form
(switch 5
  [[1] "one"]
  [[2] "two"]
  [[5 6] "five-or-six"]
  [else: "meh"])
;; -> "five-or-six"

;; No match with else
(switch 5
  [[1] "one"]
  [[2 3] "two-or-three"]
  [else: "meh"])
;; -> "meh"

;; No match and no else
(switch 5
  [[1] "one"]
  [[2] "two"])
;; -> None
```

**Also:** layout handler [`switch`](../layouts/layout-switch.md).
