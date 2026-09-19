# layout/dyn-wind
**Symbol:** `layout/dyn-wind`

**Use-case:** Block syntax for a dynamic-wind extent. Lowers to the
positional form of [`compile/dyn-wind`](../specials/compile-dyn-wind.md):
a before thunk, the body, and an after thunk.

**Behaviour:** Accepts peer blocks `before` and `after`. The primary
`dyn-wind:` block carries the body; the peers follow it in either order.
Produces `(dyn-wind BEFORE (begin BODY...) AFTER)`, where each of BEFORE
and AFTER is `(begin PEER-BODY...)` when that peer is present and `()`
when it is not. Rules: the `dyn-wind` header takes no arguments; `before`
and `after` take no arguments; neither peer may appear twice.
`compile/dyn-wind` then requires at least one of before and after, so a
`dyn-wind` block with no peer is rejected at compile time.

**Syntax:**

```
dyn-wind:
  BODY
before:
  BEFORE-BODY
after:
  AFTER-BODY
```

**Examples:**

```koskari
dyn-wind:
  (print "body")
before:
  (print "before")
after:
  (print "after")
;; -> (dyn-wind (begin (print "before"))
;;              (begin (print "body"))
;;              (begin (print "after")))
;; prints before, then body, then after

dyn-wind:
  (print "second body")
after:
  (print "second after")
;; -> (dyn-wind () (begin (print "second body")) (begin (print "second after")))
```
