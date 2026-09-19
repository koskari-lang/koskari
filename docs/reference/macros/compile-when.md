# compile/when

**Syntax:** `(when COND BODY ...)`

**Behaviour:** Expands to `(cond [COND BODY ...] [else: None])`. When
`COND` is truthy the body forms run in order and the last is the value;
otherwise the value is `None`.

**Examples:**

```koskari
(when (> n 0)
  (print n)
  n)

when (> n 0):
  print n
  n
```

```koskari
>>> (macroexpand-1 '(when a b c))
(cond [a b c] [else: None])
```

**See also:** [`unless`](compile-unless.md) for the inverted test,
[`if`](compile-if.md) for two arms.
