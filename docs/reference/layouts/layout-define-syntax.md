# layout/define-syntax
**Symbol:** `layout/define-syntax`

**Use-case:** Block surface syntax for **`define-syntax`**. With no dedicated
**`layout/define-syntax`** handler registered, the default **`layout/block`**
handler splices the transformer form(s) from the indented body after the name:
**`(define-syntax NAME TRANSFORMER …)`**.

**Behaviour:** Transformer choice and validation happen in the
**`compile/define-syntax`** macro at compile time (see
[compile/define-syntax](../specials/compile-define-syntax.md)). Does not accept
peers.

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
`special-rules`, and `special-match` use dedicated layout handlers documented
under their own pages.

**Examples:**

```koskari
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

**Implementation:** default **`layout/block`** via **`koskari/reader/layouts/basics.layout_block`**
