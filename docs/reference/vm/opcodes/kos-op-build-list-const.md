# KOS_OP_BUILD_LIST_CONST

- **Style**: CONST. Arg = constant pool index of a sequence (typically a pooled constant tuple).
- **Status**: Implemented. (`koskari/lib/opcode/BUILD_LIST_CONST.c`)
- **Stack**: `[...] → [..., list]`. Build a **fresh** list from the pooled sequence (`PySequence_List`) and push it; each eval mints a new list, never aliasing the immutable pool value.
- **Compiler form:** `compile/#list` all-constant body, or the seed of a list whose first run is constant.
