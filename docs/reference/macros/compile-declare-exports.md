# compile/declare-exports
**Syntax:** **`(declare-exports name …)`**

**Behaviour:** **`macro-rules`** → **`(define-global __all__ #(…))`** with sorted, deduplicated string names.
