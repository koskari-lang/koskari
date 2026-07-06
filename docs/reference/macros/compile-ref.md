# compile/ref
**Syntax:** **`(ref SYM)`**; **`(ref (item …))`**; **`(ref (attr …))`**; **`(ref (parameter NAME))`**;
**`(ref (environ KEY [DEFAULT]))`**.

**Behaviour:** **`compile/refq`**, **`compile/ref-item`**, **`compile/ref-attr`**, **`(ParamRef 'NAME)`**,
**`(environ-ref 'KEY [DEFAULT])`** (a ref into `os.environ`; see
[compile/define-environ](compile-define-environ.md)).

**Reader shorthand:** `&name` and `&obj.attr` (atom forms) and `&(item coll key)`
(`&` immediately followed by `(`) read as `(ref …)`. See
[bindings and refs](../concepts/bindings-and-refs.md).

