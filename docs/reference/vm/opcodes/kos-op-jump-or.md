# KOS_OP_JUMP_OR
- **Style**: LABEL. Short-circuit OR: jump if TOS is true.
- **Status**: Implemented.
- **Stack**: Pops one value; if true, push it back and jump to `arg`; if false,
  discard it and advance. Either way TOS is consumed on the fall-through path.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Compiler use-case:** `compile/or` is the canonical consumer
(`koskari/compile/specials/conditionals.py`). For `(compile/or A B C)` the
compiler emits one `JUMP_OR done` after each non-last term; the last term is
compiled without a jump. Because `JUMP_OR` already discards the value on the
fall-through path, no `POP` is needed between terms.

```
eval A
JUMP_OR done    ; truthy → push A back, jump to done
eval B          ; A was already discarded on fall-through
JUMP_OR done
eval C          ; last term — value stays on stack
done:
```

**Example:** Short-circuit or: if first operand true, jump to "ok".

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (False)| stack: [..., a] |
| 1     | JUMP_OR  | 4   | 4 (-> 4) | if a → pc=4 |
| 2     | CONST    | 1   | 1 (False)| stack: [..., b] |
| 3     | JUMP_OR  | 4   | 4 (-> 4) | if b → pc=4 |
| 4     | LABEL    | —   | —        | ok |
| 5     | RETURN   | 1   | 1        | |


## 9. Check jumps

Check jumps: if the predicate does **not** hold, jump to `arg`. All take LABEL; jump locality applies.

