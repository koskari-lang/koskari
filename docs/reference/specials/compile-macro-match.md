# compile/macro-match
**Symbol:** `compile/macro-match`

**Syntax:** `(macro-match NAME (PATTERN BODY ...) ...)`

**Use-case:** **MATCH**-dialect procedural rules, as **`compile/macro-rules`**
takes **RULE**-dialect ones.

**Behaviour:** Each rule row is **`(PATTERN BODY ...)`**, the pattern in the
explicit matcher dialect (see [`koskari.matcher`](../api/koskari-matcher.md)).
The body of the first rule whose pattern matches runs with one parameter per
pattern slot, in pattern order, each bound to what the slot matched or to
`None` when the match left it unbound. The body returns the expansion.
