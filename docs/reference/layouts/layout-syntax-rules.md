# layout/syntax-rules
**Symbol:** `layout/syntax-rules`

**Use-case:** Block syntax for the `syntax-rules` transformer within a
`define-syntax` block. Converts indented pattern/template rule blocks into the
canonical `(syntax-rules (LIT ...) ((PATTERN) TEMPLATE) ...)` form. Used as the
inner block of `layout/define-syntax`.

**Behaviour:** Accepts a literals list in the header (e.g. `[x]` or `(x y)`).
Each indented rule block has a pattern in the header and a template in the
body. Does not accept peers.

**match form:** Use `match (PATTERN):` instead of `PATTERN:` when the pattern
contains top-level keywords. Layout headers cannot parse keywords directly, but
inside parentheses the form is read in s-expr context, so keywords are valid.
Both styles expand to the same rule. Example: `ref (item obj name):` and
`match (ref (item obj name)):` produce identical output.

**Syntax:**

```
syntax-rules [LIT ...]:
  PATTERN1:
    TEMPLATE1...
  match (PATTERN-WITH-KEYWORDS):
    TEMPLATE...
  PATTERN2 ARG:
    TEMPLATE2...
```

**Examples:**

```scheme
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
  match (ref (item obj name)):
    (compile/ref-item obj name)
;; → same as ref (item obj name): ... ; use match when pattern has keywords
```

**Implementation:** `koskari/reader/layouts/define_syntax.layout_syntax_rules`

