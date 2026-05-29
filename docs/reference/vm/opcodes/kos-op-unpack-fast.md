# KOS_OP_UNPACK_FAST
- **Style**: EXTRA (`FASTVAR`, `FASTVAR`, `NOARG`, `NOARG`). Row: `a0`, `a1` =
  fast local slot indices.
- **Status**: Implemented. (`koskari/lib/opcode/UNPACK_FAST.c`)
- **Stack**: `[..., raw] → [...]`. Pop TOS, `PySequence_List`, require `len == 2`,
  then `KosNamespace_FastStore` for slot `a0` with `seq[0]` and slot `a1` with
  `seq[1]` (same ownership rules as `FAST_STORE`: namespace increments ref as
  needed).
- **Invariants**: Frame namespace supports fast locals; both indices in range
  (same as `FAST_STORE`).
- **Errors**: `ValueError` if length ≠ 2; `RuntimeError` / `NameError` if fast
  namespace invalid or index out of bounds.
- **Compiler form:** Pre-compile extra tuple `(slot0, slot1, 0, 0)` with
  integer slot indices.

---

## 5. Constants

