# KOS_OP_OBJECT_NEXT
- **Style**: COUNT. Arg = 1 or 2: 1 = TOS is iterator or async iterator (StopIteration raises on exhaustion); 2 = TOS = default, below = iterator or async iterator (StopIteration pushes default).
- **Stack**: Stack-based next from iterator or async iterator. May suspend on async iterator.
- **Invariants**: TOS (or below-TOS for arg=2) must be a sync iterator or an async iterator. A bare iterable (e.g. list, tuple) is not acceptable; use OBJECT_ITER or equivalent to obtain an iterator first. Stack layout per arg (1 or 2 elements as described).

**Example (arg=1):** TOS = iterator or async iterator; push next or raise StopIteration.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (iter) | stack: [..., iterator or async iterator] |
| 1     | OBJECT_NEXT | 1 | 1      | stack: [..., next_val] or raise StopIteration |

**Example (arg=2):** TOS = default, below = iterator or async iterator; push next or default.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (iter) | stack: [..., iterator or async iterator] |
| 1     | CONST  | 1   | 1 (99)   | stack: [..., iterator or async iterator, default] |
| 2     | OBJECT_NEXT | 2 | 2      | stack: [..., next_val] or [..., 99] if exhausted |


## 8. Jumps

