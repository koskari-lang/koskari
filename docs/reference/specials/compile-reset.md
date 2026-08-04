# compile/reset
**Symbol:** `compile/reset`

**Syntax:** `(reset BODY ...)`

**Use-case:** Establish a prompt delimiter for delimited continuations
(`compile/shift`). The body is evaluated with a prompt on the dynamic chain;
`compile/shift` inside the body can capture the continuation up to (but not
including) this prompt.

**Behaviour:** Emits `PUSH_PROMPT 0`, compiles the body with `helper_begin`,
emits `POP_PROMPT 0`.

A `shift` that cannot reach a prompt raises `KoskariPromptNotFound` (no
delimiter established) or `KoskariPromptUnreachable` (a delimiter exists but a
crossed boundary denied the capture); see the **Errors** section of
[`compile/shift`](compile-shift.md).

**Examples:**

```koskari
;; (reset (+ 1 (shift k (k 5)))) => 6
(reset
  (+
    1
    (shift k (k 5))))
```

