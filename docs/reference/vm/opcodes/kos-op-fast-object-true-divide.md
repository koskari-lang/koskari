# KOS_OP_FAST_OBJECT_TRUE_DIVIDE

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: FASTVAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/FAST_OBJECT_TRUE_DIVIDE.c`

**Example (VAR_OBJECT_ADD → FAST_OBJECT_ADD):** `(set! x (+ x 1))` compiles to value expr, VAR_OBJECT_ADD name, CONST None; VAR_OBJECT_ADD may lower to FAST_OBJECT_ADD if `x` has a slot.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 (1)    | stack: [..., 1] |
| 1     | CONST      | 1   | 1 (2)    | stack: [..., 1, 2] |
| 2     | OBJECT_ADD| 2   | 2        | stack: [..., 3] |
| 3     | RETURN     | 1   | 1        | |

**Example (OBJECT_LEN):** Unary; TOS = object. Pop 1, push len(TOS).

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | CONST     | 0   | 0 ([1,2,3]) | stack: [..., obj] |
| 1     | OBJECT_LEN| 0   | 0        | stack: [..., 3] |


## 20. Delimited continuations

Delimited continuations use a block-based layout: PUSH_PROMPT establishes the
reset and owns the COW stack block for the delimited region; PUSH_SHIFT and
POP_SHIFT delimit the shift block (handler); the continuation is the code
between POP_SHIFT and POP_PROMPT.

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
