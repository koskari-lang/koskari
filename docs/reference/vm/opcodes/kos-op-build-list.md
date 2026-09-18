# KOS_OP_BUILD_LIST

- **Style**: COUNT. Arg = number of elements.
- **Status**: Implemented. (`koskari/lib/opcode/BUILD_LIST.c`)
- **Stack**: `[..., v_0, ..., v_{n-1}] → [..., list]`. Pop n elements into a new list and push it. Empty (arg 0) pushes `[]`.
- **Compiler form:** `compile/#list` for a leading/all dynamic run (`collections.py`).
