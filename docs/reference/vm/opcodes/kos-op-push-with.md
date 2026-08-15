# KOS_OP_PUSH_WITH
- **Style**: CONST. Arg = constant pool index of binding name (symbol/string) or sentinel for no binding.
- **Status**: Implemented (single-shot with; sync and async __enter__/__exit__; VM auto-awaits when enter returns awaitable).
- **Stack before:** `[..., mgr]`. **Stack after:** body runs in a fresh operand segment.
- **Behaviour:** Pop `mgr`; resolve and cache enter/exit; call enter (auto-await if needed); on success bind enter result if name given; push new segment; look up WITH extent at current PC, compute `resume_pc = ext->pop_pc + 1`; push `KOS_DYN_WITH` with `resume_pc` stored on the node. On enter failure, no node or block is installed; exception propagates.
- **Pushes:** KOS_DYN_WITH (single-shot; transfer barrier). Node stores `resume_pc` (instruction after POP_WITH) for use when exit swallows an exception.
- **Invariants:** Node owns one operand segment; stack restoration is structural (segment pop), not height-mark. A WITH extent must exist at the PUSH_WITH PC; otherwise RuntimeError.

