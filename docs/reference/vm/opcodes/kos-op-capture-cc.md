# KOS_OP_CAPTURE_CC
- **Style**: COUNT, 0 or 1.
- **Stack**: Captures the continuation of the CC dynamic extent (from current pc
  until the extent is exited at POP_CC); pushes it (KosPyContinuation) onto
  stack. With arg 0 (`let/cc`) the stack effect is `[...] → [..., continuation]`.
  With arg 1 (`call/cc`) the callee on TOS is set aside for the capture and put
  back beneath the continuation, `[..., fn] → [..., fn, continuation]`, so the
  captured stack holds the form's enclosing operands and not the callee; the
  CALL 1 that follows applies fn to the continuation. Invoking the continuation
  is a non-local jump to the POP_CC opcode with a value pushed onto the stack.
- **Example:** See the PUSH_CC disassembly table: at index 1, CAPTURE_CC pushes
  the continuation. Invoking it (e.g. CALL 1 with 42) jumps to POP_CC (index 4)
  with stack [..., 42]; POP_CC runs; execution continues at index 5 (resume).


## 22. Dynamic wind

Unified extent wind/unwind uses `kos_extent_run_wind` and `kos_extent_run_unwind`,
which dispatch by dynamic-chain node kind for simple pop and transfer plans.
See [compile/capture-cc](../../specials/compile-capture-cc.md) and
[compile/dyn-wind](../../specials/compile-dyn-wind.md).

