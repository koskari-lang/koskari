# compile/reset
**Symbol:** `compile/reset`

**Syntax:** `(reset BODY ...)`

**Use-case:** Establish a prompt delimiter for delimited continuations
(`compile/shift`). The body is evaluated with a prompt on the dynamic chain;
`compile/shift` inside the body can capture the continuation up to (but not
including) this prompt.

**Behaviour:** Emits `PUSH_PROMPT 0`, compiles the body with `helper_begin`,
emits `POP_PROMPT 0`.

**Examples:**

```scheme
;; (reset (+ 1 (shift k (k 5)))) => 6
(reset
  (+
    1
    (shift k (k 5))))
```

