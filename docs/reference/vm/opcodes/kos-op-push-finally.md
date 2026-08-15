# KOS_OP_PUSH_FINALLY
- **Style**: LABEL. Arg = handler/finally target.
- **Status**: Implemented.
- **Stack**: Pushes FINALLY node and a KosSegment (like PUSH_TRY). Exception dispatch clears the block before jumping to the finally handler. POP_FINALLY drops the block (with PopBlock(1) to promote one value to parent); block must have exactly one value when POP_FINALLY runs. Node unwind drops the block when control passes through.
- **Pushes:** KOS_DYN_FINALLY (static extent KOS_EXT_FINALLY) and KosSegment.
- **Invariants**: Extent kind KOS_EXT_FINALLY.

**Example:** Push finally block. Arg = finally block pc (or extent holds it).

| index | opcode      | arg | arg view | comment |
|------:|-------------|-----|----------|---------|
| 0     | PUSH_FINALLY| 3   | 3 (-> 3) | push FINALLY; finally code at 3 |
| 1     | …           | …   | …        | protected body |
| 2     | POP_FINALLY | 0   | 0        | normal exit |
| 3     | LABEL       | —   | —        | finally block |
| 4     | END_FINALLY | 0   | 0        | (conceptual) |

