# compile/let
**Symbol:** `compile/let`

**Syntax:**

- Unnamed: `(let BINDINGS BODY ...)`
- Named: `(let NAME BINDINGS BODY ...)`

**Bindings:** A list of binding forms; each is `[SYM EXPR]` or `(EXPR as: SYM)`.
Parsed with `gather_bindings` (see helpers). Matcher shape: **BINDINGS** with
[`koskari.matcher`](../../api/koskari-matcher.md).

**Use-case:** Lexical bindings for the duration of the body. Unnamed let pushes a
child namespace (PUSH_NS), binds names to compiled values, runs the body, and
pops (POP_NS). Named let expands to an immediately-invoked named lambda:
`((lambda NAME [args...] BODY...) val...)`, so the name is bound in the
function's scope for recursion.

**Behaviour:** Unnamed: `child_namespace` with the binding names and value
expressions; body compiled with `helper_begin`. Named: rewrite to
`((lambda NAME [args...] BODY...) val...)` via `pycons`.

**Examples:**

```koskari
;; Unnamed let
(let [[x 1] [y 2]] (+ x y))

;; Named let (e.g. loop)
(let loop [[n 10]]
  (loop ...))
```

