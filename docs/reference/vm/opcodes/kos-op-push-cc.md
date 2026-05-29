# KOS_OP_PUSH_CC
- **Style**: NOARG. The instruction has no argument. The resume PC (instruction
  index to continue at when the continuation is invoked) is **not** stored in
  the bytecode; the evaluator obtains it from the **block table** (extent
  table): the CC extent for this PUSH_CC has `pop_pc` at the matching POP_CC,
  and the resume PC is `pop_pc + 1` (the instruction immediately after POP_CC).
- **Stack**: Pushes CC block onto dynamic chain. Normal exit: body runs, then
  POP_CC runs, then execution continues at the next instruction (resume). On
  invoke: jump to POP_CC with value on stack; POP_CC runs; continue at next
  instruction (same resume PC from block table).
- **Pushes:** KOS_DYN_CC (static extent KOS_EXT_CC).
- **Example:** (call/cc (lambda (k) (k 42))) → 42. Body captures k, pushes 42,
  CALL 1 invokes k(42); that jumps to POP_CC (index 4) with stack [..., 42];
  POP_CC runs; execution continues at 5 (resume), RETURN 1 yields 42.

| index | opcode     | arg | arg view   | comment |
|------:|------------|-----|------------|---------|
| 0     | PUSH_CC    | 0   | 0          | push CC block; resume at 5 (= pop_pc+1 from block table) |
| 1     | CAPTURE_CC | 0   | 0          | stack before: [...]; after: [..., continuation] |
| 2     | CONST      | 0   | 0 (42)     | stack: [..., continuation, 42] |
| 3     | CALL       | 1   | 1          | invoke continuation with one arg → jump to POP_CC (4) with [..., 42] |
| 4     | POP_CC     | 0   | 0          | invoke lands here with value on stack; pop CC; next pc = 5 (resume) |
| 5     | RETURN     | 1   | 1          | yield TOS (42) |

