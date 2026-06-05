# compile/set!
**Symbol:** `set!` (`compile/set!`)

**Syntax:** Plain **`(set! SYM EXPR)`**; **`(set! (values PATTERN ...) VALUE)`**;
augmented **`(set! SYM (OP SYM VAL))`**; **`(set! (item …) VAL)`**; **`(set! (attr …) VAL)`**;
**`(set! (deref REF) VAL)`** (and **`compile/item`**, **`compile/attr`**, **`compile/deref`**
variants).

**Behaviour:** Expands to **`setq`**, **`compile/setq-values`**, **`compile/inplace`**, **`compile/set-item`**, **`compile/set-attr`**, or **`compile/set-ref`**.

