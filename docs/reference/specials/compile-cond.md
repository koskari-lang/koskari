# compile/cond

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/cond`

**Syntax:** `(compile/cond [TEST BODY...] [TEST BODY...] ... [else: BODY...])`

**Use-case:** Multi-branch conditional. Evaluates clauses in order; the first
truthful test's body is evaluated and returned. No match yields `None`. The
`else:` keyword as test is always true (catch-all).

**Behaviour:** For each clause `(test body)`, emits LABEL, compiles test,
JUMP_FALSE to next clause, compiles body with `helper_begin`, JUMP to done.
If test is `else:`, compiles body and breaks. If no else clause, emits CONST
None for the fall-through case.

**Examples:**

```scheme
;; Empty cond → None
(compile/cond)

;; Single clause
(compile/cond [True 100])

;; else-only
(compile/cond [else: 100])

;; Multi-clause with else fallthrough
(compile/cond
  [False 100]
  [True 101]
  [else: 102])
```

**Note:** The higher-level forms `compile/if`, `compile/when`, and
`compile/unless` are macros in `koskari/bootstrap/_conditionals.ki` (see [MACROS.md](../../design/MACROS.md)).

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
