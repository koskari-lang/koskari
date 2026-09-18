# KOS_OP_PUSH_DYN_WITH
- **Style**: CONST. Arg = constant pool index of binding name or sentinel for no binding (same as PUSH_WITH).
- **Stack / behaviour:** Same as PUSH_WITH except pushes KOS_DYN_DYN_WITH. Node participates in continuation transfer plans (wind/unwind on non-local transfer).
- **Pushes:** KOS_DYN_DYN_WITH.

