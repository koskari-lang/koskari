# KOS_OP_UNPACK_VAR
- **Style**: EXTRA (`VAR`, `VAR`, `NOARG`, `NOARG`). Row: `a0`, `a1` = constant
  pool indices of the two names (strings).
- **Stack**: `[..., raw] → [...]`. No values pushed; **STORE** semantics only
  (same as `VAR_STORE` / `KosNamespace_Store` through `frame_ns_store`): `seq[0]`
  into the first name, `seq[1]` into the second. Pop TOS, `PySequence_List`,
  require `len == 2`, then store; release the temporary list.
- **Invariants**: Both names must already be bound in the frame namespace (see
  §1.9 STORE vs BIND).
- **Errors**: `ValueError` if length ≠ 2.
- **Compiler form:** Pre-compile extra tuple `(name0, name1, 0, 0)`; names
  intern to cpool like other `VAR` extra fields.

