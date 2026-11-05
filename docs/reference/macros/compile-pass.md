# compile/pass

**Syntax:** `(pass)`, or a bare `pass`

**Behaviour:** A no-op expression whose value is `None`.
It fills a body or an arm that has to hold a form but has nothing to do.

`pass` also carries a bare arm, so a lone `pass` on a line works. The
`compiler/bare-control-words` pragma governs that: `convert` (the default)
accepts it, `warn` accepts it with a diagnostic, and `error` rejects it.

**Examples:**

```koskari
>>> (if False 1 (pass))
None
```

```koskari
>>> def try-it []:
...   try:
...     (raise (ValueError "x"))
...   except [ValueError]:
...     pass
>>> (try-it)
None
```

**See also:** [`compile/begin`](../specials/compile-begin.md),
[`pragma`](compile-pragma.md),
[`bare-syntax`](../specials/compile-bare-syntax.md).
