# layout/while
**Symbol:** `layout/while`

**Use-case:** Block syntax for a `while` loop with an optional `else`
peer. The read result keeps the `while` head and is compiled by
[`compile/while`](../specials/compile-while.md).

**Behaviour:** The header is `while TEST`, one test expression. Body
forms are appended to the header list with no `begin` wrapper, giving
`(while TEST BODY...)`. `else` is the only peer gathered; its body is
appended as a final `(else: BODY...)` clause rather than merged into the
loop body. Rules: `else` takes no arguments, and at most one `else` peer
may appear.

**Syntax:**

```
while TEST:
  BODY
else:
  BODY
```

**Examples:**

```koskari
while (< x 3):
  (print x)
;; -> (while (< x 3) (print x))

while (< x 3):
  (print x)
  set! x (+ x 1)
else:
  (print "done")
;; -> (while (< x 3) (print x) (set! x (+ x 1)) (else: (print "done")))
```

```koskari
define total 0
define x 0

while (< x 3):
  set! total (+ total x)
  set! x (+ x 1)
else:
  (print "while done" total)
;; prints: while done 3
```
