# KOS_OP_CONS
- **Style**: COUNT. Arg = number of values on stack (bottom = first element,
  top = tail).
- **Stack**: `[..., e_0, ..., e_{count-1}] → [..., result]` where result =
  (e_0 . (e_1 . ... (e_{count-1}) ...)). Implemented via KosStack_TailPtr,
  KosPair_ConsFromPtr, KosStack_Drop, then push result. count 0 → push
  nil (INCREF).

