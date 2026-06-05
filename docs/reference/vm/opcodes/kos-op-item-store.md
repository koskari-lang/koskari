# KOS_OP_ITEM_STORE
- **Style**: NOARG. Key and value from stack.
- **Status**: Implemented.
- **Stack**: `[..., obj, key, value] → [..., obj]`. Set item.
- **Invariants**: TOS = value, below = key, below = object.

**Example:** obj[key] = value.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | VAR        | 0   | 0 ('d')  | stack: [..., d] |
| 1     | CONST      | 1   | 1 ('k')  | stack: [..., d, 'k'] |
| 2     | CONST      | 2   | 2 (42)   | stack: [..., d, 'k', 42] |
| 3     | ITEM_STORE | 0   | 0        | d['k'] = 42; stack: [..., d] |

