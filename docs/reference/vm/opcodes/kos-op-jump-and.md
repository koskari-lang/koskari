# KOS_OP_JUMP_AND
- **Style**: LABEL. Short-circuit AND: jump if TOS is false.
- **Status**: Implemented.
- **Stack**: Pops one value; if false, push it back and jump to `arg`; if true,
  discard it and advance. Either way TOS is consumed on the fall-through path.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Compiler use-case:** `compile/and` is the canonical consumer
(`koskari/compile/specials/conditionals.py`). For `(and A B C)` the
compiler emits one `JUMP_AND done` after each non-last term; the last term is
compiled without a jump. Because `JUMP_AND` already discards the value on the
fall-through path, no `POP` is needed between terms.

```
eval A
JUMP_AND done   ; falsy → push A back, jump to done
eval B          ; A was already discarded on fall-through
JUMP_AND done
eval C          ; last term — value stays on stack
done:
```

**Example:** Short-circuit and: if first operand false, jump to "fail".

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | CONST     | 0   | 0 (True) | stack: [..., a] |
| 1     | JUMP_AND  | 4   | 4 (-> 4) | if not a → pc=4 |
| 2     | CONST     | 1   | 1 (True) | stack: [..., b] |
| 3     | JUMP_AND  | 4   | 4 (-> 4) | if not b → pc=4 |
| 4     | LABEL     | —   | —        | fail (or result) |

