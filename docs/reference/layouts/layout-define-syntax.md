# layout/define-syntax

```{admonition} Reference status
:class: note

Adapted from design/LAYOUTS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `layout/define-syntax`

**Use-case:** Block surface syntax for **`define-syntax`**. In the full builtins
environment, **`basics.ki`** defines **`define layout/define-syntax layout/flat`**, so
this head uses the same splicing behaviour as **`layout/flat`**: the read result
is **`(define-syntax NAME TRANSFORMER …)`** with the transformer form(s) from the
indented body inlined after **`NAME`**, not wrapped in an extra **`compile/begin`**
at the block level.

**Behaviour:** Implemented only as that **`layout/flat`** alias in **`basics.ki`**.
Transformer choice and validation happen in the **`compile/define-syntax`** macro
at compile time (see **compile/define-syntax** in [MACROS.md](../../design/MACROS.md)). Does not
accept peers.

**Syntax:**

```
define-syntax NAME:
  syntax-rules [LIT ...]:
    PATTERN1:
      TEMPLATE1...
    PATTERN2:
      TEMPLATE2...
```

**Other inner heads:** `macro-rules`, `syntax-match`, `macro-match`,
`special-rules`, and `special-match` use the same indentation style as their
layout sections below.

**Examples:**

```scheme
define-syntax do-something-with-x:
  syntax-rules [x]:
    do-something-with-x:
      (print "x and no args" x)
      x
    do-something-with-x y:
      (print "x and one arg" x y)
      (+ x y)
;; → (define-syntax do-something-with-x
;;     (syntax-rules (x)
;;       ((do-something-with-x) (compile/begin (print "x and no args" x) x))
;;       ((do-something-with-x y) (compile/begin (print "x and one arg" x y) (+ x y)))))
```

**Implementation:** `koskari/importer/basics.ki` (**`define layout/define-syntax layout/flat`**); handler code in **`koskari/reader/layouts/basics.layout_flat`**.

**Tests:** `tests/reader/layouts/test_define_syntax.py`

---

Source: [design/LAYOUTS.md](../../design/LAYOUTS.md)
