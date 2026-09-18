# KOS_OP_EXCEPTION_CLEAR
- **Style**: NOARG.
- **Stack**: No change. Clear current exception state.
- **Invariants**: —

**Example:** After handling, clear exception before continuing.

| index | opcode         | arg | arg view | comment |
|------:|----------------|-----|----------|---------|
| 4     | EXCEPTION_CLEAR| 0   | 0        | clear exception state |
| 5     | RETURN         | 1   | 1        | |

