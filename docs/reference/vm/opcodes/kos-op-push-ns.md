# KOS_OP_PUSH_NS
- **Style**: EXTRA. Schema: CONST (cpool index of NamespaceSpec), CONST (cpool index of names tuple or None), NOARG, NOARG. Instruction `arg` is the extra-row index. Binding count is `len(names)` when names is a tuple, or `0` when names is `None`.
- **Constant pool**: NSPEC and NAMES are loaded from the pool; they are not placed on the stack by preceding `CONST` opcodes.
- **Stack**: If `opa == 0`, no stack operands for bindings. If `opa > 0`, stack is `[..., val_0, ..., val_{opa-1}]` with TOS = last value; names come from the extra row. The VM binds `names[i]` to `val[i]`, then drops `opa` stack slots.
- **Pushes:** KOS_DYN_NS (static extent KOS_EXT_NS; owns its operand segment; the prior namespace returns on POP_NS when the enclosing node is revealed).
- **Invariants**: Extent kind KOS_EXT_NS.

**Example:** Push namespace with no bound values (names = None). Operand row holds NSPEC and None; stack carries only the chain side effect until body runs.

| index | opcode  | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | PUSH_NS| 0   | extra row 0 (e.g. c# nspec, c# None) | push NS region; chain: [..., KosDynNs] |
| 1     | …      | …   | …        | body |
| 2     | POP_NS | 0   | 0        | pop NS; restore previous namespace |

