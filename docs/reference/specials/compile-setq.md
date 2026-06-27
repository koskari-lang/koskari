# compile/setq

**Symbol:** `compile/setq`

**Syntax:** `(setq SYM EXPR)`

**Use-case:** Store the value of EXPR into the variable named SYM (must already
exist in scope or be requested from an enclosing scope). The form evaluates to
None.

**Behaviour:** Requests the variable name, compiles EXPR, emits VAR_STORE, then
pushes CONST None so the form has a value.

**Note:** The `set!` macro expands symbol assignment to this form; multi-value
assignment uses `setq-values`. Augmented symbol assignment uses
[`compile/inplace`](compile-inplace.md). Item and attribute in-place updates use
[`compile/inplace-item`](compile-inplace-item.md) and
[`compile/inplace-attr`](compile-inplace-attr.md). Plain item, attribute, and ref
stores use the corresponding set specials.

**Examples:**

```koskari
(setq x 42)
(setq counter (+ counter 1))
```
