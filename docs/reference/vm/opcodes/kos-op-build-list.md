# KOS_OP_BUILD_LIST

- **Style**: COUNT. Arg = number of elements.
- **Stack**: `[..., v_0, ..., v_{n-1}] → [..., list]`. Pop n elements into a new list and push it. Empty (arg 0) pushes `[]`.
- **Compiler form:** `compile/#list` for a leading/all dynamic run (`collections.py`).
