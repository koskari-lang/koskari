# KOS_OP_MATCH_ITEMS

- **Style**: NOARG
- **Stack**: `[...]` -> `[..., items]`; cannot miss.
- **Semantics**: Push a new empty list.
- **Compiler form:** Emitted by the match compiler at the start of a repeat;
  the repeat's passes append to the list.
