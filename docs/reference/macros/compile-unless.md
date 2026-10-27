# compile/unless

**Syntax:** `(unless COND BODY ...)`

**Behaviour:** Expands to `(cond [COND None] [else: BODY ...])`. When
`COND` is falsy the body forms run in order and the last is the value;
otherwise the value is `None`.

**Examples:**

```koskari
def invite [user]:
  unless user:
    return "missing user"
  f"sent to {user.email}"
```

```koskari
>>> (unless False 1 2)
2
>>> (macroexpand-1 '(unless a b c))
(cond [a None] [else: b c])
```

**See also:** [`when`](compile-when.md).
