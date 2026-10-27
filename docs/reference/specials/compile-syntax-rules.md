# compile/syntax-rules
**Symbol:** `compile/syntax-rules`

**Syntax:** `(syntax-rules NAME LITERALS RULE ...)`

**Use-case:** A **`syntax-rules`**-style transformer. The transformer is
the **value** of the form; bind it with **`(var NAME ...)`**.

**Behaviour:** Each rule row is **`(PATTERN TEMPLATE)`**. A symbol in
**`LITERALS`** matches itself; any other symbol in a pattern is a pattern
variable. The head of each pattern binds the symbol the macro was invoked
as, so a template may use it. The value is a `Syntax` whose arms are the
rules in order: a use expands the template of the first rule whose pattern
matches.

**Ellipsis and nesting:** A pattern variable followed by **`...`** captures zero
or more matches. Ellipsis is **depth-aware**: a variable captured under *N*
nested **`...`** must appear under *N* nested **`...`** in the template, and each
template **`...`** expands one level. Referencing such a variable under **fewer**
ellipses than it was captured is a compile-time error.

```koskari
(var regroup
  (syntax-rules regroup []
    [(regroup (a b ...) ...)
     (result (a b ...) ...)]))

(regroup (1 2 3) (4 5))
;; => (result (1 2 3) (4 5))
```

Here `a` is captured under one ellipsis and `b` under two; the template mirrors
that nesting, so each inner group expands within its outer iteration. This is
what lets macros like [`flet`](../macros/compile-flet.md) and
[`labels`](../macros/compile-labels.md) carry a per-binding body
(**`body ...`**) nested inside a per-binding declaration list.

**See also:** [Macros and hygiene](../../guide/macros-and-hygiene.md),
[Hygiene](../../guide/hygiene.md).
