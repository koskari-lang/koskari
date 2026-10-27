# layout/for-each
**Symbol:** `layout/for-each`

**Use-case:** Block syntax for a `for-each` loop with an optional `else`
peer. The read result keeps the `for-each` head and is compiled by
[`compile/for-each`](../specials/compile-for-each.md).

**Behaviour:** The header is `for-each [BINDING SEQ]`, a binding form and
the sequence it draws from. Body forms are appended to the header list
with no `begin` wrapper, giving `(for-each [BINDING SEQ] BODY...)`.
`else` is the only peer gathered; its body is appended as a final
`(else: BODY...)` clause rather than merged into the loop body. Rules:
`else` takes no arguments, and at most one `else` peer may appear.

**Syntax:**

```
for-each [BINDING SEQ]:
  BODY
else:
  BODY
```

**Examples:**

```koskari
for-each [x (range 0 3)]:
  (print x)
;; -> (for-each [x (range 0 3)] (print x))

for-each [x seq]:
  (print x)
else:
  done
;; -> (for-each [x seq] (print x) (else: done))
```

```koskari
for-each [i (range 0 3)]:
  (print "item" i)
else:
  (print "for-each done")
;; prints: item 0
;; prints: item 1
;; prints: item 2
;; prints: for-each done
```

**Also:** special form [`for-each`](../specials/compile-for-each.md).
