# compile/bind!
**Syntax:** Value binding; **`(bind! name (item …))`** / **`(attr …)`**; **`(dynamic r …)`**;
**`(constant value)`**; **`(memoized getfn)`**; **`(parameter param-name r …)`**;
**`(environ key [default])`**.

**Behaviour:** **`compile/bindq`** with **`Ref`** types as appropriate. The
**`environ`** form binds the name to a ref into `os.environ` for the string
`key` (see [compile/define-environ](compile-define-environ.md)).

