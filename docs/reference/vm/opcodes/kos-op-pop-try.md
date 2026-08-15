# KOS_OP_POP_TRY
- **Style**: NOARG. Argument unused (or 0).
- **Status**: Implemented.
- **Stack**: Promote one value: pop TOS from the try segment, pop the region (dropping its segment), and push the value onto the revealed segment. The segment must hold exactly one value. Clear exception state before pop.
- **Invariants**: Top of chain must be KOS_DYN_TRY.

