# KOS_OP_PUSH_TRY
- **Style**: LABEL. Handler targets in extent table.
- **Status**: Implemented.
- **Stack**: Pushes TRY node and a KosSegment (like PUSH_LOOP). Exception dispatch clears the segment before jumping to the handler. POP_TRY promotes the protected result (exactly one value) to the revealed segment and drops the region's segment. Node unwind (e.g. BREAK through TRY) also drops the segment.
- **Pushes:** KOS_DYN_TRY (static extent KOS_EXT_TRY; handler targets in extent descriptor) and KosSegment.
- **Invariants**: Extent kind KOS_EXT_TRY. Handler targets (except_pc, finally_pc, exit_pc) from extent descriptor.

**Example:** try/except. Extent table holds except_pc, finally_pc, exit_pc.

| index | opcode  | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | PUSH_TRY | 0  | 0        | push TRY region; extent table: except_pc, etc. |
| 1     | …      | …   | …        | try body |
| 2     | POP_TRY | 0   | 0        | normal exit: pop TRY |
| 3     | JUMP   | 5   | 5 (-> 5)  | skip handler |
| 4     | LABEL  | —   | —        | except handler (except_pc) |
| 5     | LABEL  | —   | —        | exit |

