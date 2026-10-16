# KOS_OP_COMBINE_KWDS
- **Style**: COUNT. Arg = count of dicts to merge.
- **Stack**: `[..., d_0, ..., d_{m-1}] → [..., dict]`. Merge the m dicts (later overwrites earlier on key clash) and push result.
