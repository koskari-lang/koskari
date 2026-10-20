# KOS_OP_MATCH_UNBOUND

- **Style**: EXTRA (RAW): cursor depth.
- **Stack**: `[...]` -> `[..., c]`; cannot miss.
- **Semantics**: Push a cursor at the position of the cursor at depth `a0`
  whose end is its subject's end: the elements a `MATCH_BOUND` left before
  that cursor's end, at any depth of nesting, are left on the new cursor. The
  cursor at `a0` does not move.
- **Compiler form:** Emitted by the match compiler for a negation. Its pattern
  is tested on the new cursor against the elements left in the subject,
  whatever follows the negation, and the new cursor is popped on both exits,
  so the negation consumes no element.
