# compile/bindq
**Symbol:** `compile/bindq`

**Syntax:** `(bindq SYM EXPR)`

**Use-case:** Bind the name SYM to a Ref. EXPR must evaluate to a Ref, such
as `(refq x)`, `(ref-attr obj name)` or `(ref-item obj key)`. Reading SYM
afterwards dereferences through that Ref, and storing into SYM with `setq`
writes through it. The form evaluates to None.

**Behaviour:** Declares the variable name, compiles EXPR, emits VAR_BIND,
which installs the Ref on the stack as the binding of SYM, then pushes
CONST None. A plain value is not boxed into a Ref: `(bindq local 10)` puts
the integer where a Ref belongs. To store a value, write `(var local 10)`
for a new variable or `(setq local 10)` for an existing one.

**Examples:**

```koskari
(let [[x 77]]
  (bindq r (refq x))
  r)                               ; 77, read through the Ref to x

(bindq count (ref-attr obj count)) ; count now aliases obj.count
```
