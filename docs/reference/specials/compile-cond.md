# compile/cond
**Symbol:** `compile/cond`

**Syntax:** `(cond [TEST BODY...] [TEST BODY...] ... [else: BODY...])`

**Use-case:** Multi-branch conditional. Evaluates clauses in order; the first
truthful test's body is evaluated and returned. No match yields `None`. The
`else:` keyword as test is always true (catch-all).

**Behaviour:** For each clause `(test body)`, emits LABEL, compiles test,
JUMP_FALSE to next clause, compiles body with `helper_begin`, JUMP to done.
If test is `else:`, compiles body and breaks. If no else clause, emits CONST
None for the fall-through case.

**Examples:**

```koskari
;; Empty cond → None
(cond)

;; Single clause
(cond [True 100])

;; else-only
(cond [else: 100])

;; Multi-clause with else fallthrough
(cond
  [False 100]
  [True 101]
  [else: 102])
```

**Note:** The higher-level forms `compile/if`, `compile/when`,
`compile/unless`, `compile/?`, and `compile/bool` are macros in
`koskari/bootstrap/_conditionals.ki` (see [Macros](../macros/index.md)).

