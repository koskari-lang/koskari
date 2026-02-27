# Dyn Type Install/Uninstall Stack Block Audit

Audit of stack block PushBlock/DropBlock pairing for each dyn type, to verify
no mismatch that could cause leaked or orphaned blocks.

## Summary Table

| Dyn Type   | PushBlock (Install)                          | DropBlock (Uninstall/Teardown)                    | Matched? |
|------------|----------------------------------------------|--------------------------------------------------|----------|
| PROMPT     | kos_dyn_install (KosStackBlock_PushBlock)    | kos_dyn_uninstall_handler (KosStackBlock_DropBlock) | ✓ |
| SHIFT      | none                                         | none                                             | ✓ |
| TRY        | PUSH_TRY opcode                              | kos_dyn_uninstall_handler                        | ✓ |
| FINALLY    | PUSH_FINALLY opcode                          | kos_dyn_uninstall_handler                        | ✓ |
| LOOP       | PUSH_LOOP opcode                             | kos_dyn_uninstall_handler; POP_LOOP opcode       | ✓ |
| FOREACH    | PUSH_FOREACH opcode                          | kos_dyn_uninstall_handler; POP_LOOP; BREAK; CONT | ✓ |
| WITH       | eval.c kos_with_push                         | kos_dyn_uninstall_handler                        | ✓ |
| DYN_WITH   | eval.c kos_dyn_with_push                     | kos_dyn_uninstall_handler; POP_DYN_WITH; TRUNCATE | ✓ |
| PARAM      | no stack block                               | no stack block                                   | ✓ |
| Others     | no stack block                               | no stack block                                   | ✓ |

## Per-Type Detail

### KOS_DYN_PROMPT

- **Install**: `kos_dyn_install` case KOS_DYN_PROMPT → `KosStackBlock_PushBlock(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)))` (dynchain.c:1713)
- **Uninstall**: `kos_dyn_uninstall_handler` case KOS_DYN_PROMPT → `KosStackBlock_DropBlock(...)` (dynchain.c:1750)
- **Other teardown**: `xfer_step_truncate_and_inject` drops PROMPT nodes when freeing chain (dynchain.c:3539–3543)

### KOS_DYN_SHIFT

- **Header comment** (dynchain.h:195): "shift: install pushes stack block, teardown drops it"
- **Implementation**: kos_dyn_install has no case for SHIFT (falls to default, returns 0). kos_dyn_uninstall_handler has no case for SHIFT (falls to default, returns 0).
- **Design docs** (OPCODES.md, FEATURES.md, bytecode.md): "PROMPT owns the stack block; SHIFT is a simple marker, no stack block"
- **Conclusion**: Header comment is wrong/outdated. SHIFT does not push or drop; PROMPT owns the block. Implementation matches design docs.

### KOS_DYN_TRY, KOS_DYN_FINALLY

- **Install**: PUSH_TRY / PUSH_FINALLY opcodes call `KosStackBlock_PushBlock` then `kos_dyn_push` (opcode/PUSH_TRY.c:27, PUSH_FINALLY.c:27)
- **Uninstall**: `kos_dyn_uninstall_handler` case TRY/FINALLY/LOOP/FOREACH → `KosStackBlock_DropBlock` (dynchain.c:1768)

### KOS_DYN_LOOP, KOS_DYN_FOREACH

- **Install**: PUSH_LOOP / PUSH_FOREACH opcodes call `KosStackBlock_PushBlock` then `kos_dyn_push` (opcode/PUSH_LOOP.c:52, PUSH_FOREACH.c:94)
- **Uninstall**: `kos_dyn_uninstall_handler` case TRY/FINALLY/LOOP/FOREACH → `KosStackBlock_DropBlock` (dynchain.c:1768)
- **Other**: POP_LOOP opcode also drops block (opcode/POP_LOOP.c:38). BREAK and CONTINUE drop blocks for each popped LOOP/FOREACH (opcode/BREAK.c:63,89; CONTINUE.c:67).

### KOS_DYN_WITH, KOS_DYN_DYN_WITH

- **Install**: eval.c `kos_with_push` / `kos_dyn_with_push` call `KosStackBlock_PushBlock` then `kos_dyn_push_with` (eval.c:1161, 1224)
- **Uninstall**: `kos_dyn_uninstall_handler` case WITH/DYN_WITH → `KosStackBlock_DropBlock` (dynchain.c:1806)
- **Other**: POP_WITH / POP_DYN_WITH opcodes drop block (opcode/POP_WITH.c:52, POP_DYN_WITH.c:44). `xfer_step_truncate_and_inject` drops DYN_WITH when truncating (dynchain.c:3539–3543).

### KOS_DYN_PARAM

- **Install**: kos_dyn_install case PARAM → `kos_dynparam_enter` (no stack block)
- **Uninstall**: kos_dyn_uninstall_handler case PARAM → `kos_dyn_param_unwind` (no stack block)

### Other Types (FRAME, WIND, NS, CC, YIELD_FROM, CC_RAISE)

- No stack block push/drop in install or uninstall.

## Transfer Plan Paths

### xfer_step_install

- Does **not** call kos_dyn_install for nodes in the segment.
- Restores stack from plan's resume_stack. No PushBlock per node.

### xfer_step_truncate_and_inject

- Explicitly drops blocks for PROMPT and DYN_WITH before freeing the chain (dynchain.c:3531–3547).
- This is necessary because TRUNCATE does not run uninstall handlers; it frees nodes directly.

### xfer_step_pop_one

- Runs `kos_dyn_uninstall` for the popped node, which dispatches to `kos_dyn_uninstall_handler` → DropBlock for types that push blocks.

## Resolved Issue

The **dynchain.h** `u.shift` comment previously read "install pushes stack block, teardown drops it",
which contradicted both the implementation (no push/drop for SHIFT) and the design docs (PROMPT owns
the block). This comment has been corrected; it now reads: "shift: simple marker; PROMPT owns the
stack block for the delimited region." No implementation changes were required.
