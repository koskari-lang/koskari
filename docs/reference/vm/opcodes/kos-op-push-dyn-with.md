# KOS_OP_PUSH_DYN_WITH
- **Style**: CONST. Arg = constant pool index of binding name or sentinel for no binding (same as PUSH_WITH).
- **Stack / behaviour:** Same as PUSH_WITH except that the node is one a continuation transfer runs: a transfer leaving the body calls exit, and one resuming a captured body calls enter again.
- **Pushes:** KOS_DYN_WITH, run by transfers.

