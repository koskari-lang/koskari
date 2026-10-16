# KOS_OP_MATCH_ITEMS

- **Style**: NOARG
- **Stack**: `[...]` -> `[..., list]`; cannot miss.
- **Semantics**: Push an empty list: the items a repeat's passes append to through
  `MATCH_ITERATION`, which sits under each pass's mark while the passes run.
  After the loop the compiler stores it into the capturing slot or pops it.
- **Compiler form:** Emitted by the match compiler before a repeat whose passes
  are kept: one that captures, has a minimum count, or binds names inside.
