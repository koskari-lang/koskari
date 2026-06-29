# KOS_OP_DEREF
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: `[..., ref] → [..., value]`. Pop one value (a Ref with a deref method). Call `ref.deref()`, push result. Raises TypeError if TOS has no deref method.
- **Use-case**: Fast path for dereferencing a mutable reference. Used by the `(compile/deref EXPR)` special form.

**Example:** Dereference a ref. For `(deref r)` where r holds a Ref: VAR (or FAST) loads r, DEREF calls ref.deref().

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR    | 0   | 0 ('r')  | stack: [..., ref] |
| 1     | DEREF  | 0   | 0        | stack: [..., value] |


## 16. Argument production

