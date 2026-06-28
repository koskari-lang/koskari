# compile/ref
**Syntax:** **`(ref SYM)`**; **`(ref (item …))`**; **`(ref (attr …))`**; **`(ref (parameter NAME))`**.

**Behaviour:** **`compile/refq`**, **`compile/ref-item`**, **`compile/ref-attr`**, **`(ParamRef 'NAME)`**.

**Reader shorthand:** `&name` and `&obj.attr` (atom forms) and `&(item coll key)`
(`&` immediately followed by `(`) read as `(ref …)`. See
[bindings and refs](../concepts/bindings-and-refs.md).

