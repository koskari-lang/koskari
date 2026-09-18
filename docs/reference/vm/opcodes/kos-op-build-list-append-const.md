# KOS_OP_BUILD_LIST_APPEND_CONST

- **Style**: CONST. Arg = constant pool index of a single value.
- **Stack**: `[..., list] → [..., list]`. Append the pooled value to the TOS list in place. TypeError when TOS is not a list.
- **Compiler form:** `compile/#list` tail constant run of length 1.
