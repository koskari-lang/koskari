# layout/if
**Symbol:** `layout/if`

**Use-case:** Multi-branch conditional with optional `elif` and `else` peers.
Lowers to `compile/cond`.

**Behaviour:** Accepts optional peer blocks `elif` and `else`. Gathers contiguous
peer blocks at the same indent level after the `if` body. Produces
`(compile/cond (TEST BODY) (TEST BODY) ... (else: BODY))`. Rules: `elif` cannot
follow `else`; `else` takes no arguments; only one `else` is allowed.

**Syntax:**

```
if COND:
  BODY
elif COND:
  BODY
else:
  BODY
```

**Examples:**

```koskari
if x:
  y
;; → (compile/cond (x y))

if x:
  a
elif y:
  b
else:
  c
;; → (compile/cond (x a) (y b) (else: c))
```

**Implementation:** `koskari/reader/layouts/conditionals.layout_if`

