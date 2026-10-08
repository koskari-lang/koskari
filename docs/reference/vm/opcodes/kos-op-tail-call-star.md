# KOS_OP_TAIL_CALL_STAR
- **Style**: STAR. Arg = bitflags (1 = has *args, 2 = has **kwargs).
- **Stack**: Same layout as CALL_STAR: `[..., callable, pos, kwd]`. Pop order and the tuple coercion as CALL_STAR; the call then proceeds as TAIL_CALL describes, with `pos` and `kwd` spread.

---


## 18. Block control

