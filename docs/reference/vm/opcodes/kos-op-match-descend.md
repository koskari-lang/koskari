# KOS_OP_MATCH_DESCEND

- **Style**: LABEL
- **Stack**: `[..., v]` -> `[..., cur]`; on a miss `[..., v]` -> `[...]` and a
  jump to the label.
- **Semantics**: The sequence gate on `v` read through `Datum`. A pair spine
  (`nil` included), a tuple, a list or another sequence type descends, and a
  cursor over it replaces `v`; `str`, `bytes`, `bytearray`, a mapping, a set,
  an iterator and any other value miss. A pair spine is counted once, here: an
  improper spine is a miss and a spine that loops raises `ValueError`. Later
  instructions read a list at its live length and any other sequence at the
  length it had here.
- **Compiler form:** Emitted by the match compiler for a sequence pattern and
  for the subject of a standalone matcher.
