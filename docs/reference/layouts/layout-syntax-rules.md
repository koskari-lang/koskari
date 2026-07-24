# layout/syntax-rules
**Symbol:** `layout/syntax-rules`

**Use-case:** Block syntax for the `syntax-rules` transformer within a
`define-syntax` block. Converts indented pattern/template rule blocks into the
canonical `(syntax-rules (LIT ...) ((PATTERN) TEMPLATE) ...)` form. Used as the
inner block of `layout/define-syntax`.

**Behaviour:** Accepts a literals list in the header (e.g. `[x]` or `(x y)`).
Each indented rule block has a pattern in the header and a template in the
body. Does not accept peers.

**case form:** Use `case (PATTERN):` instead of a bare header when the pattern
is most clearly written as a single parenthesized s-expression. The `case`
wrapper unwraps at layout time; both styles expand to the same rule. Example:
`ref (item obj name):` and `case (ref (item obj name)):` produce identical
output. Keyword atoms are valid in layout headers (see
[Reader and layout syntax](../reader-layout.md)); `case` is for grouping the
full pattern, not a workaround for keywords.

**Syntax:**

```
syntax-rules [LIT ...]:
  PATTERN1:
    TEMPLATE1...
  case (PATTERN):
    TEMPLATE...
  PATTERN2 ARG:
    TEMPLATE2...
```

**Examples:**

```koskari
syntax-rules [x]:
  do-something-with-x:
    (print "no args" x)
    x
  do-something-with-x y:
    (print "one arg" x y)
    (+ x y)
;; → (syntax-rules (x)
;;     ((do-something-with-x) (compile/begin (print "no args" x) x))
;;     ((do-something-with-x y) (compile/begin (print "one arg" x y) (+ x y))))

syntax-rules [item]:
  case (ref (item obj name)):
    (compile/ref-item obj name)
;; → same as ref (item obj name): ...
```

**Implementation:** `koskari/reader/layouts/define_syntax.layout_syntax_rules`

