# KOS_OP_VAR_LIST_APPEND

- **Style**: VAR. Arg = variable name (may lower to FAST_LIST_APPEND).
- **Status**: Implemented. (`koskari/lib/opcode/VAR_LIST_APPEND.c`)
- **Stack**: `[..., value] → [...]`. Append TOS to the list bound to the named variable, in place. Used by `compile/#list-append` on a symbol binding.
