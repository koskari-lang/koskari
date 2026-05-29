# KOS_OP_PUSH_DYN_WITH
- **Style**: CONST. Arg = constant pool index of binding name or sentinel for no binding (same as PUSH_WITH).
- **Status**: Implemented (multi-shot dyn-with; sync and async enter/exit; VM auto-awaits when enter returns awaitable).
- **Stack / behaviour:** Same as PUSH_WITH except pushes KOS_DYN_DYN_WITH. Node participates in continuation transfer plans (wind/unwind on non-local transfer).
- **Pushes:** KOS_DYN_DYN_WITH.

