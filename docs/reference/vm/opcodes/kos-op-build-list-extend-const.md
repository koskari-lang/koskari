# KOS_OP_BUILD_LIST_EXTEND_CONST

- **Style**: CONST. Arg = constant pool index of a sequence (typically a pooled constant tuple).
- **Status**: Implemented. (`koskari/lib/opcode/BUILD_LIST_EXTEND_CONST.c`)
- **Stack**: `[..., list] → [..., list]`. Extend the TOS list in place from the pooled sequence (tuple fast path, general-iterable fallback). TypeError when TOS is not a list.
- **Compiler form:** `compile/#list` tail constant run of length ≥ 2.
