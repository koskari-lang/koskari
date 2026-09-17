# KOS_OP_CHECK_NIL

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., x] → [...]`. Pop TOS; branch to arg when it is not an
  empty-list singleton; fall through when it is.
- **Invariants**: Jump locality for target. The squashed form of
  [NILP](#kos_op_nilp) followed by JUMP_FALSE.
- **Compiler form:** Produced by `Compiler.compress`; pre-compile arg = string
  label, post-compile arg = pc.
- **Implementation**: `koskari/lib/opcode/CHECK_NIL.c`
