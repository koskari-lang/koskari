# Dynchain usage inventory (sibilant-ng)

Generated for replacing `koskari/lib/dynchain.c` while preserving **all symbols and behaviors** referenced from outside that file. Line numbers are approximate; use search when refactoring.

## 1. Include graph

| Header | Role |
|--------|------|
| `koskari/lib/lib.h` | Includes `dynchain.h` before `eval.h`; most `koskari/lib/*.c` units include `lib.h`. |
| `koskari/lib/eval.h` | Includes `dynchain.h`; defines `KosContext` fields that **are** the dynamic chain and transfer state. |

There is no separate opcode include tree: opcode `*.c` files are compiled with the same headers as the eval core (via generated build / unity pattern used in this project).

## 2. `KosContext` fields (eval.h) — dynchain / transfer / delimited control

These fields **must** remain coherent with any new implementation:

| Field | Purpose (summary) |
|-------|-------------------|
| `dyn_top` | Top of `KosDynNode` chain |
| `frame_top` | Cached top `KosDynFrame` |
| `block` | Address of innermost operand-stack `KosStackBlock *` (refreshed with chain) |
| `globals_frame` | Outermost frame for `GLOBALS` |
| `globals_cache_version` | Invalidation vs `chain_version` |
| `token` / `domain` | Evaluator identity / ambient KosDomain |
| `unwind_target_frame` | Unwind targeting |
| `pending_return_value` / `pending_return_to_parent` | Async wind / return deferral |
| `plan` | Active `KosTransferPlan *` |
| `plan_stack` | Suspended nested plans |
| `chain_version` | Bumped on chain mutation |
| `py_resume_inject` / `py_resume_kind` / `py_await_iter` | Python await/yield bridge |
| `inject_*` | Exception injection (incl. dynchain-driven paths) |
| `suspension_event` | Last boundary reason for revive |
| `flags` | `KosContextFlags` |

Delimited **invoke return** metadata lives on **`KOS_DYN_DELIM`** (`KosDynDelim_*`, `kos_dyndelim_*`), not in `KosContext` fields.

Accessor macros include: `KosContext_DYN_TOP`, `KosContext_FRAME_TOP`, `KosContext_BLOCK`, `KosContext_PLAN`, `KosContext_CHAIN_VERSION`, `KosContext_PY_AWAIT_ITER`, … (see `eval.h`).

## 3. Types and macros from `dynchain.h` (externally visible)

### 3.1 Enums / constants

`KosDynKind`, `KosDynFlags`, `KosDynWindPhase`, `KosRegionStyle`, `KosXferEvent`, `KosHandleResult`, `KOS_DYN_CAP_*`, `KosYfPhase`, `KOS_YF_*` action constants, `KOS_WITH_PHASE_*`, `KOS_PROMPT_ID_UNDELIMITED`, `KosTransitionKind` / `KosTransitionReq` / `KosTransitionResult` (used with `kos_dyn_transition`).

### 3.2 Structs

`KosDynNode` (tagged union), all `KosDyn*` view types, `KosTransferPlan`, `KosContinuation`, `KosTransferAction` (via plan), continuation/install structs as declared.

### 3.3 Macro surface (non-exhaustive but mandatory)

**Node / spine:** `KosDynNode_KIND`, `PARENT`, `FLAGS`, `CAPS`, `NODE_ID`, `HAS_CAP`, all `KosDynNode_IS_*`.

**Payload:** All `KosDynFrame_*`, `KosDynPrompt_*`, `KosDynWind_*`, `KosDynTry_*`, `KosDynFinally_*`, `KosDynLoop_*`, `KosDynForeach_*`, `KosDynNs_*` / `KosDynCollect_SAVED_NS`, `KosDynYieldFrom_*`, `KosDynParam_*`, `KosDynWith_*`, `KosDynCC_*`, `KosDynCCRaise_*`, `KosDynGen_*`.

**Plans:** `KosTransferPlan_*` macros — **eval.c** mutates `CONTINUATION_REF`, `ORIGINAL_TARGET`, `RESUME_PC`, `RESUME_FRAME`, `RESUME_STACK`, `STEP_RESUME_INJECT`, `ACTION_INDEX`, `ACTION_COUNT`, `SUSPENDED`.

**Continuations:** `KosContinuation_*` — **cont.c** reads `PROMPT_ID`, `TOKEN_REF`.

## 4. Functions declared in `dynchain.h` — external reference map

Legend: **Ext** = referenced outside `dynchain.c` (must remain linkable and behavior-compatible). **Int** = only used inside `dynchain.c` in the current tree (still part of public header; replacement may keep or make `static` after single-TU merge).

| Function | Ext | Notes |
|----------|-----|-------|
| `kos_dynnode_kind_name` | Y | Opcodes / POP_PARAM debug |
| `kos_dynframe_new` / `clone` / `free` | Y | eval, tests, demand, coro |
| `kos_dynprompt_new` / `free` | Y | tests/native |
| `kos_dynwind_new` / `clone` / `free` | Y | tests/native |
| `kos_dyntry_new` / `clone` / `free` | Y | tests/native |
| `kos_dynfinally_new` / `clone` / `free` | Y | tests/native |
| `kos_dynloop_new` / `clone` / `free` | Y | tests/native |
| `kos_dynforeach_new` / `clone` / `free` | Y | tests/native |
| `kos_dynnode_free` / `kos_dynnode_clone` | Int | |
| `kos_dynchain_free` | Y | eval, tests |
| `kos_clone_chain_segment` | Y | tests |
| `kos_dyn_push` | Int | |
| `kos_dyn_uninstall` (`kos_dyn_pop`) | Int | |
| `kos_dyn_push_clone` | Int | |
| `kos_dyn_push_frame` / `kos_dyn_push_frame_empty` | Y | callable |
| `kos_dynframe_reset_for_tail` | Y | callable |
| `kos_dyngen_new` / `free` / `acquire` / `release` | Y | eval, demand, coro, iter, tests |
| `kos_dyn_gen_install` / `kos_dyn_gen_unwind_exhausted` | Y | eval, demand, iter |
| `kos_dyn_pop_frame` | Y | callable |
| `kos_dyn_attach_frame` | Int | |
| `kos_dyn_detach_top_frame` | Y | eval, utils, RETURN, YIELD |
| `kos_dyn_push_prompt` / `kos_dyn_pop_prompt` | Y | opcodes |
| `kos_dyn_push_cc` / `kos_dyn_pop_cc` | Y | opcodes |
| `kos_dyn_push_shift` | Y | PUSH_SHIFT |
| `kos_build_cc_unwind_plan` | Y | eval |
| `kos_dyn_push_wind` / `kos_dyn_pop_wind` | Y | opcodes |
| `kos_dyn_push_try` / `finally` / `loop` / `foreach` | Y | opcodes |
| `kos_dyn_pop_try` / `finally` / `loop` / `foreach` | Y | opcodes |
| `kos_dyn_push_ns` / `kos_dyn_pop_ns` | Y | PUSH_NS, POP_NS |
| `kos_dyn_push_collect` / `kos_dyn_pop_collect` / `kos_dyn_collect_unwind` | Y | POP_COLLECT path |
| `kos_dyn_push_param` / `kos_dyn_push_param_dict` / `kos_dyn_pop_param` | Y | PUSH_PARAM, POP_PARAM |
| `kos_dyn_push_with` / `dyn_with` / provisional variants | Y | PUSH_WITH, PUSH_DYN_WITH |
| `kos_dynwith_clone` / `kos_dyndynwith_clone` | Int | |
| `kos_dyn_pop_with` | Y | opcodes |
| `kos_dyn_with_unwind` | Y | POP_WITH, POP_DYN_WITH |
| `kos_dyn_pop_top` | Y | POP_WIND |
| `kos_dyn_unwind_to_frame` / `walk_return` / `unwind_to_node` | Int | transition internals |
| `kos_dynnode_on_control_returned` | Y | eval main loop |
| `kos_dyn_find_pending_exit` / `kos_dyn_complete_pending_exit` / `clear` | Y | eval, wrappers |
| `finalize_provisional_with_enter` | Int | |
| `kos_dyn_install` / `kos_dyn_wind` / `kos_dyn_unwind` | Int | |
| `kos_dyn_exception_unwind` | Int | |
| `kos_dyn_handle` | Int | |
| `kos_dyn_ns_unwind` / `kos_dyn_yieldfrom_unwind` | Int | |
| `kos_find_node_for_raise` | Int | |
| `kos_find_node_for_loop` | Y | eval, RETURN, ITER_NEXT |
| `kos_dyn_transfer_to_loop` | Int | |
| `kos_dyn_transition` | Y | eval, utils, many opcodes |
| `kos_dyn_push_yield_from` / `kos_dyn_pop_yield_from` | Y | YIELD_FROM |
| `kos_find_prompt` / `kos_find_cc` | Y | PUSH_SHIFT, etc. |
| `kos_validate_capture` | Int | |
| `kos_shift_capture` | Y | PUSH_SHIFT |
| `kos_capture_cc` | Y | CAPTURE_CC |
| `kos_continuation_free` / `free_exclude` / `clone` | Y | opcodes, cont, eval |
| `kos_compute_lca` | Y | tests |
| `kos_build_transfer_plan` / `direct_install` / `delimited_push` / `return` / `install_only` | Y | tests; eval uses return + cc_unwind |
| `kos_dyn_build_pop_shift_plan` / `kos_dyn_build_push_shift_unwind_plan` | Y | POP_SHIFT, PUSH_SHIFT |
| `kos_run_transfer_step` | Y | eval |
| `kos_free_transfer_plan` | Y | eval, opcodes, tests |
| `kos_suspend_transfer_plan` / `kos_resume_transfer_plan` | Y | nested invoke during transfer; eval_chain replan |
| `kos_free_transfer_plan_stack` | Y | fatal transfer errors in eval_chain |
| `kos_invoke_continuation` | Y | eval, INVOKE_K*, cont |
| `kos_dynnode_traverse` / `kos_dynchain_traverse` | Y | eval, iter |
| `kos_continuation_traverse` / `kos_transferplan_traverse` | Y | eval, cont |
| `kos_dyn_walk_handle_yield` | Int | |
| `kos_dyn_clone` | Int | |
| `koskari_dynchain_init` / `koskari_dynchain_clear` | Y | eval init, test_harness |

**Note:** Marking **Int** only reflects current call sites; the **public header still exports** these symbols — a shared library or future TU could use them. Prefer keeping exports unless the branch explicitly narrows ABI.

**`KosEvalState` entry points:** Transfer-plan builders (`kos_build_transfer_plan`,
`kos_build_return_plan`, `kos_build_delimited_push_plan`, `kos_build_direct_install_plan`,
`kos_build_install_only_plan`), `kos_suspend_transfer_plan`, `kos_resume_transfer_plan`,
and `kos_invoke_continuation` take **`KosEvalState *es`** as their first parameter.
`kos_run_transfer_step` already did. Builders call **`KosEvalState_SyncFromContext`**
on exit. Dynamic-cache refresh for **`frame_top`** / **`block`** is
**`KosEvalState_RefreshDynCaches(es)`**; innermost stack owner lookup is
**`KosEvalState_InnermostStackOwner(es)`** (declared in **`eval.h`**).
Reserved hook runner typedef **`KosHookInvokeFn`** also passes **`KosEvalState *es`**
instead of **`KosContext *ctx`**.

## 5. Per-file call sites (outside `dynchain.c`)

### 5.0 Headers mentioning dynchain types (no extra `kos_*` calls)

| File | Reference |
|------|-----------|
| `lib.h` | `struct KosDynNode` forward declaration; includes `dynchain.h`. |
| `iter.h` | `void *gen` documented as `KosDynGen *` when set. |
| `demand.h` | Comments reference `kos_dyn_gen_install`. |
| `stack.h` | Comment: region metadata lives in dynchain nodes. |
| `ref.h` | `KosDynamicRef` is unrelated to `KosDyn*` (name collision only); do not confuse. |

### 5.1 `koskari/lib/eval.c`

- **Transfer plans:** `kos_free_transfer_plan`, `kos_run_transfer_step`, `kos_build_return_plan`, `kos_build_cc_unwind_plan`, `KosTransferPlan_*` macros, `kos_invoke_continuation`.
- **Chain / frames:** `kos_dynchain_free`, `kos_dyn_detach_top_frame`, `kos_dynframe_free`, `kos_dynframe_new`, `kos_dynframe_clone`, `KosContext_DYN_TOP` assignment, `KosDynNode_PARENT`, `CURRENT_FRAME` / `KosDynFrame_*` throughout.
- **Decorators:** Walk `dyn_top` via `KosDynNode_PARENT` for `__koskari_dynchain__` exception attribute.
- **Gen:** `kos_dyngen_release`, `kos_dyn_gen_install`, `KosDynGen_*`, `kos_dyn_gen_unwind_exhausted`.
- **GC traverse:** `kos_dynnode_traverse` (per-kind helpers incl. GEN), `kos_dynchain_traverse`, `kos_transferplan_traverse`.
- **Pending with / wind exit:** `kos_dyn_complete_pending_exit`, `kos_dyn_clear_pending_exit` (via wrappers).
- **Tail / async:** `kos_find_node_for_loop`, `KosDynForeach_*` for exhausted foreach.
- **Main loop:** `kos_dynnode_on_control_returned`, `kos_dyn_transition` (incl. `KOS_TRANS_TRY_LOCAL_RAISE`, walk handle yield).
- **Init:** `koskari_dynchain_init`, `koskari_dynchain_clear`.
- **`kos_yieldfrom_close_delegate`** (`koskari/lib/dynchain/dyn_yield_from.c`): `KosDynYieldFrom_*` field access; `KOS_YF_*` kinds/actions in `dynchain.h`.

### 5.2 `koskari/lib/callable.c`

- `KosDynNode_PARENT`, `kos_dynframe_reset_for_tail`, `kos_dyn_push_frame_empty`, `kos_dyn_pop_frame`.

### 5.3 `koskari/lib/utils.c`

- `kos_dyn_transition`, `kos_dyn_detach_top_frame`, `kos_dynframe_free`, `KosDynFrame_NATIVE_HANDLER`.

### 5.4 `koskari/lib/demand.c`

- `kos_dyngen_new`, `kos_dynframe_new`, `KosDynGen_*`, `kos_dyngen_free`, `kos_dynframe_free`, `kos_dyngen_acquire`, `KosContext_DYN_TOP`, `kos_dyn_gen_install`.

### 5.5 `koskari/lib/coro.c`

- Same generator frame pattern as demand: `kos_dyngen_new`, `kos_dynframe_new`, `KosDynGen_ACTIVE`, `kos_dyngen_free`, etc.

### 5.6 `koskari/lib/iter.c`

- `KosDynGen`, `KosContext_DYN_TOP`, `kos_dyngen_release`, `kos_dyn_gen_install`, `kos_dyn_gen_unwind_exhausted`, `kos_dynchain_free`, `kos_dynnode_traverse`.

### 5.7 `koskari/lib/cont.c`

- `KosContinuation_*`, `KOS_PROMPT_ID_UNDELIMITED`, `kos_invoke_continuation`, `kos_continuation_free` / `free_exclude`, `kos_continuation_traverse`, `KosContext_TOKEN`.

### 5.8 `koskari/lib/object.c` / `koskari/lib/func.c`

- `KosDynFrame_STACK` via `CURRENT_STACK` / frame helpers only (no direct `kos_dyn_*` calls in grep).

### 5.9 `koskari/lib/bytecode.c` / `bytecode.h`

- `kos_shift_ic_for` — used by PUSH_SHIFT (shift resume PC).

### 5.10 Opcodes (`koskari/lib/opcode/*.c`)

| File | Symbols / patterns |
|------|---------------------|
| `BREAK.c` / `CONTINUE.c` | `kos_dyn_transition` |
| `RETURN.c` | `kos_dyn_transition`, `kos_dyn_detach_top_frame`, `kos_dynframe_free`, `KosDynNode_PARENT`, `KosDynNode_IS_GEN`, `kos_find_node_for_loop`, `KosDynForeach_*`, `KosDynFrame_NATIVE_HANDLER` |
| `YIELD.c` | `KosDynNode_PARENT`, `KosDynNode_IS_GEN`, `kos_dyn_detach_top_frame` |
| `YIELD_FROM.c` | Extensive `KosDynYieldFrom_*`, `kos_dyn_push_yield_from`, `kos_dyn_pop_yield_from`, `KosContext_DYN_TOP`, `KosDynFrame_*`, `KosDynNode_IS_YIELD_FROM` |
| `POP_PROMPT.c` | `KosContext_DYN_TOP`, `KosDynNode_IS_PROMPT`, `kos_dyn_transition`, `kos_dynnode_kind_name`, `KosDynFrame_*`, `kos_dyn_pop_prompt` |
| `POP_SHIFT.c` | `KosDynNode_*`, chain walk `PARENT`, `kos_dyn_build_pop_shift_plan`, `KosTransferPlan_*`, `kos_free_transfer_plan` |
| `PUSH_SHIFT.c` | `kos_find_prompt`, `KosDynPrompt_PENDING_SHIFT_WRAPPER`, `kos_dyn_push_shift`, `kos_shift_capture`, `kos_continuation_free`, `kos_dyn_build_push_shift_unwind_plan`, `KosTransferPlan_CONTINUATION_REF`, `kos_shift_ic_for`, `KosDynFrame_ACTING_PC` |
| `POP_WIND.c` | `kos_dyn_pop_top`, `KosDynWind_*`, `KosDynNode_PARENT` |
| `PUSH_WIND.c` | `kos_dyn_push_wind`, `kos_dyn_wind`, `KosDynWind_ENTERED` |
| `PUSH_WITH.c` | `kos_dyn_push_with_provisional`, `kos_dyn_wind`, `kos_dyn_pop_with`, `KosDynFrame_ACTING_PC` |
| `POP_WITH.c` / `POP_DYN_WITH.c` | `kos_dyn_with_unwind`, `kos_dyn_pop_with`, `KosDynNode_IS_*` |
| `PUSH_DYN_WITH.c` | `kos_dyn_push_dyn_with_provisional`, `kos_dyn_wind`, `kos_dyn_pop_with` |
| `PUSH_TRY` / `PUSH_FINALLY` / `PUSH_LOOP` / `PUSH_FOREACH` | `kos_dyn_push_*`, rollback pops on failure |
| `POP_TRY` / `POP_FINALLY` / `POP_LOOP` / `POP_FOREACH` | `kos_dyn_pop_*`, `KosDynFinally_*`, `KosDynNode_IS_REGION` |
| `PUSH_NS` | `kos_dyn_push_ns` |
| `POP_NS` / `POP_COLLECT` / `POP_PARAM` | corresponding `kos_dyn_pop_*`, `kos_dynnode_kind_name` |
| `PUSH_PROMPT` | `kos_dyn_push_prompt` |
| `PUSH_CC` | `kos_dyn_push_cc` |
| `POP_CC` | `kos_dyn_pop_cc` |
| `PUSH_PARAM` | `kos_dyn_push_param_dict` |
| `CAPTURE_CC` | `kos_capture_cc`, `kos_continuation_free` |
| `INVOKE_K.c` / `INVOKE_K_VAR.c` | `kos_invoke_continuation` |
| `CALL*.c` / `TAIL_CALL.c` | `KosDynFrame_STACK` |
| `GLOBALS.c` / `IMPORT_REL.c` | Walk `KosDynNode` from frame toward root, `KosDynNode_IS_FRAME`, `KosContext_GLOBALS_FRAME`, `KosDynFrame_NS` |
| `CLEAR_TRY` / `CLEAR_FINALLY` / `CLEAR_LOOP` / `CLEAR_FOREACH` | `KosDynNode_IS_REGION`, enable flags on region nodes |
| `ITER_NEXT.c` | `kos_find_node_for_loop`, `KosDynForeach_*` |

### 5.11 Tests

| File | Role |
|------|------|
| `tests/native/test_dynchain.c` | Direct unit tests: allocators, clone, `kos_dynchain_free`, `kos_clone_chain_segment`, `kos_compute_lca`, `kos_build_transfer_plan`, `kos_free_transfer_plan` |
| `tests/native/test_eval.c` | `kos_dynframe_*`, `kos_dyngen_*`, integration |
| `tests/native/test_harness.c` | `koskari_dynchain_init`, `run_dynchain_tests` |

### 5.12 Python / introspection

- `eval.c` sets exception attribute `__koskari_dynchain__` (frame list for diagnostics). Replacement must preserve shape expected by tests or document breakage.

## 6. Patterns that tie opcodes to chain shape

1. **Manual chain walks:** `POP_SHIFT.c` walks from `dyn_top` with `KosDynNode_PARENT` until `KosDynNode_IS_PROMPT`. `GLOBALS.c` / `IMPORT_REL.c` walk from current frame to find outer frames.
2. **Top must match opcode:** Most `POP_*` assume their node kind is `dyn_top`.
3. **Delimited continuation:** **`KOS_DYN_DELIM`** nodes (`kos_find_nearest_delim`, `kos_build_delimited_push_plan`, …) interact with `POP_PROMPT` and transfer plans (`eval.c` / `dynchain.c`).
4. **Generator:** `KosDynGen` linked under frame; `iter.c` / `demand.c` clear `DYN_TOP` and install via `kos_dyn_gen_install`.

## 7. Checklist for implementor

- [ ] Every **Y** row in §4 links and runs.
- [ ] Every opcode file in §5.10 covered by tests or manual verification.
- [ ] `KosContext` layout and macro offsets unchanged for embedded/extension consumers.
- [ ] `__koskari_dynchain__` behavior unchanged or documented.
- [ ] Full `tox` / Makefile test targets green.
