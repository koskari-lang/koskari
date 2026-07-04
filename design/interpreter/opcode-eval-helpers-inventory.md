# Opcode helpers inventory (`eval.c` and `opcode/*.c`)

This document lists **static (or file-local) helper functions** used when implementing
koskari opcodes. The VM lives in `koskari/lib/eval.c` (not CPython `ceval.c`).

**Why opcodes can call `eval.c` static helpers**

Opcode bodies are not separate link units: each `koskari/lib/opcode/*.c` file is
`#include`d into `eval.c` via `koskari/lib/handlers.incl` *after* the helper
definitions that appear earlier in `eval.c`. Those helpers are therefore in the
same translation unit as every `KOS_OP_HANDLER(...)`.

**Scope**

- **Included**: helpers defined as `static` in `eval.c` or in an opcode `.c` file,
  and `KOS_OP_INLINE` helpers in `eval.c` (e.g. `push_result`).
- **Excluded**: symbols already exported for other TUs (`kos_push_or_await`,
  `KosContext_Call`, `kos_dyn_*`, `kos_extent_*`, etc.) except where opcodes still
  call the internal name—see notes below.
- **Excluded**: the eval loop itself (`eval_phase_*`, `process_raise`, …)—not
  invoked from opcode fragments.

---

## 1. Helpers defined in `eval.c` (used from opcode bodies)

The table lists **current internal names**, a **suggested public name** for a
future `koskari` runtime API (native modules, tooling), and **opcodes that call
the helper** (by `KOS_OP_HANDLER` basename).

**Koskari async gen flattening:** **`op_attach_koskari_agen_frame`**, **`op_attach_koskari_agen_frame_for_iter_next`**, and **`op_attach_koskari_agen_frame_for_yield_from`** each take **`KosEvalState *es`** as the first argument (the same live eval state as the opcode handler). Each validates **`gen`** and performs opcode-specific failure cleanup, then calls **`kos_attach_agen_gen_flatten(es, agen, gen)`**, which clears **`agen->ctx`**’s dyn/frame head and invokes **`kos_dyn_gen_install(es, gen)`** so **`KosEvalState_RefreshDynCaches`** updates **`es`** in place.

| Internal name | Suggested public name | Opcodes using it |
|---------------|----------------------|------------------|
| `push_result` | `KosEval_PushResult` (or `KosContext_PushResult`) | ATTR, ATTR_REF, CAPTURE_CC, CAR, CDR, CONS, CONST, CONST_ITEM, CONST_ITEM_REF, DEREF, EXCEPTION, EXCEPTION_MATCH, FAST, FAST_ATTR, FAST_ATTR_REF, FAST_REF, GLOBALS, IMPORT, IMPORT_FROM, IMPORT_REL, ITEM, ITEM_REF, LOCALS, OBJECT_AITER, OBJECT_BIT_INVERT, OBJECT_DIR, OBJECT_DIVMOD, OBJECT_IS_INSTANCE, OBJECT_IS_SUBCLASS, OBJECT_ITER, OBJECT_LEN, OBJECT_MODULO, OBJECT_NOT, OBJECT_POWER, OBJECT_REPR, OBJECT_TRUTH, OBJECT_TYPE, OBJECT_VARS, OBJECT_FORMAT, OBJECT_FORMAT_CONST, PARAM, PAIR_SPLICE, PROPER, PUSH_SHIFT, VAR, VAR_ATTR, VAR_ATTR_REF, VAR_REF |
| `op_get_next_from_iter` | `KosEval_GetNextFromIter` | ITER_NEXT, NEXT |
| `op_attach_koskari_agen_frame` | `KosEval_AttachKosAsyncGenForNext` | NEXT |
| `op_attach_koskari_agen_frame_for_iter_next` | `KosEval_AttachKosAsyncGenForForeach` | ITER_NEXT |
| `op_attach_koskari_agen_frame_for_yield_from` | `KosEval_AttachKosAsyncGenForYieldFrom` | YIELD_FROM |
| `get_await_next` | `KosEval_GetAwaitNext` | YIELD_FROM (async path; `NEXT` mentions it in comments only) |
| `eager_step_awaitable` | `KosEval_EagerStepAwaitable` | YIELD_FROM |
| `kos_return_delimited` | `KosEval_ReturnDelimited` | RETURN |
| `get_name_from_insn` | `KosCode_NameFromInsn` | IMPORT, IMPORT_FROM, IMPORT_REL, PUSH_DYN_WITH, PUSH_WITH, VAR_BIND, VAR_OBJECT_INPLACE_*, VAR_REF, VAR_STORE |
| `get_prompt_id_from_insn` | `KosCode_PromptIdFromInsn` | PUSH_SHIFT |
| `frame_ns_deref` | `KosFrame_NSDeref` | CALL_VAR, CALL_VAR_ATTR, CALL_VAR_ATTR_NAMES, CALL_VAR_ATTR_STAR, CALL_VAR_NAMES, CALL_VAR_STAR, VAR_ATTR, VAR_ATTR_CLEAR, VAR_ATTR_REF, VAR_ATTR_STORE, VAR_OBJECT_INPLACE_* |
| `frame_ns_store` | `KosFrame_NSStore` | UNPACK_VAR, VAR_OBJECT_INPLACE_*, VAR_STORE |
| `frame_ns_bind` | `KosFrame_NSBind` | VAR_BIND |
| `frame_ns_ref` | `KosFrame_NSRef` | VAR_REF |
| `kos_resolve_named_attr` | `KosEval_ResolveNamedAttr` | CALL_ATTR, CALL_ATTR_NAMES, CALL_ATTR_STAR, CALL_FAST_ATTR, CALL_FAST_ATTR_NAMES, CALL_FAST_ATTR_STAR, CALL_VAR_ATTR, CALL_VAR_ATTR_NAMES, CALL_VAR_ATTR_STAR |
| `resolve_context_enter_exit` | `KosWith_ResolveContextEnterExit` | PUSH_DYN_WITH, PUSH_WITH |
| `kos_await_dispatch` | `KosEval_AwaitDispatch` (public wrapper may already be desirable; `kos_push_or_await` exists) | AWAIT |
| `obj_op_reducing` | `KosEval_ObjectOpReducing` | OBJECT_ADD, OBJECT_BIT_AND, OBJECT_BIT_OR, OBJECT_BIT_XOR, OBJECT_FLOOR_DIVIDE, OBJECT_MATRIX_MULTIPLY, OBJECT_MULTIPLY, OBJECT_STR, OBJECT_SUBTRACT, OBJECT_TRUE_DIVIDE |
| `obj_op_binary` | `KosEval_ObjectOpBinary` | OBJECT_BIT_LEFT, OBJECT_BIT_RIGHT, OBJECT_IN, OBJECT_IS, OBJECT_IS_NOT, OBJECT_NOT_IN, OBJECT_INPLACE_* |
| `op_OBJECT_EQ_impl` | `KosEval_ObjectCompare` | OBJECT_EQ, OBJECT_GE, OBJECT_GT, OBJECT_LE, OBJECT_LT, OBJECT_NE |
| `kos_check_cmp` | `KosEval_CheckCompare` | CHECK_EQ, CHECK_GE, CHECK_GT, CHECK_LE, CHECK_LT, CHECK_NE |
| `kos_check_contains` | `KosEval_CheckContains` | CHECK_IN, CHECK_NOT_IN |
| `kos_check_identity` | `KosEval_CheckIdentity` | CHECK_IS, CHECK_IS_NOT |
| `kos_check_two_arg` | `KosEval_CheckTwoArgPredicate` | CHECK_IS_INSTANCE, CHECK_IS_SUBCLASS |
| `binary_in_result`, `binary_not_in_result`, `binary_is_result`, `binary_is_not_result` | Keep internal or expose as `KosEval_*` callbacks | Passed into `obj_op_binary` from OBJECT_IN, OBJECT_NOT_IN, OBJECT_IS, OBJECT_IS_NOT |

**Already public from `eval.c` (opcode bodies use the public name)**

- `kos_push_or_await` — wraps static `push_or_await`. Used by CALL, CALL_ATTR*,
  CALL_FAST*, CALL_NAMES, CALL_VAR*.

**Static helpers in `eval.c` not referenced from opcode fragments**

Examples: `next_from_python_async_iter`, `next_from_python_sync_iter` (only used
inside `op_get_next_from_iter`), `defer_drive` (defer / YIELD_FROM control inside
eval), `push_or_await` (implementation behind `kos_push_or_await`), exception /
transfer machinery, etc.

---

## 2. Helpers defined in opcode `.c` files (file-local `static`)

These are **only** visible inside the including `.c` file (one opcode file per
include, so no cross-opcode sharing unless moved to a shared header or `eval.c`).

### 2.1 `YIELD_FROM.c`

| Helper | Role | Used by |
|--------|------|---------|
| `yield_from_normalize` | Classify delegate (`KOS_YF_*`), reject coroutines, `GetIter`, etc. | YIELD_FROM |
| `yield_from_rollback_pc` | PC rollback for suspend/resume | YIELD_FROM |
| `yield_from_emit_yield` | Emit yield boundary through eval conventions | YIELD_FROM |
| `yield_from_discard_stop_iteration_value` | StopIteration payload handling | YIELD_FROM |
| `yield_from_drive_sync_iter` | Drive sync iterator delegate | YIELD_FROM |
| `yield_from_drive_sync_gen` | Drive `PyGen` delegate | YIELD_FROM |
| `yield_from_drive_py_async` | Drive `__anext__` / async gen | YIELD_FROM |
| `yield_from_drive_kos_async_gen` | Drive `KosPyAsyncGen` flattened into the same eval (`op_attach_koskari_agen_frame_for_yield_from`, stack handoff) | YIELD_FROM |

**Suggested public grouping**: `KosYieldFrom_*` (normalize, drive-by-kind, emit
yield, rollback PC) — aligns with `design/interpreter/iteration-yield-boundaries.md`.

### 2.2 `CALL_*_STAR.c` / `TAIL_CALL_STAR.c`

| Helper | File | Role |
|--------|------|------|
| `ensure_tuple` | CALL_STAR.c | Coerce `*args` slot to an exact tuple |
| `ensure_tuple_star` | TAIL_CALL_STAR.c | Same pattern for tail star-call |
| `ensure_tuple_fast_star` | CALL_FAST_STAR.c | Same |
| `ensure_tuple_var_star` | CALL_VAR_STAR.c | Same |
| `ensure_tuple_attr_star` | CALL_ATTR_STAR.c | Same |
| `ensure_tuple_fast_attr_star` | CALL_FAST_ATTR_STAR.c | Same |
| `ensure_tuple_var_attr_star` | CALL_VAR_ATTR_STAR.c | Same |

**Redundancy**: Implementations are the same “if not `PyTuple_CheckExact`, replace
with `PySequence_Tuple`” pattern. **Suggested public**: one
`KosEval_EnsureTupleExact(PyObject **val)` used by all star-call opcodes.

### 2.3 `PAIR_SPLICE.c`

| Helper | Role |
|--------|------|
| `pair_splice_extend` | Extend a list from a segment (pair unpack, tuple fast path, generic iter) |

**Suggested public**: `KosQuasiquote_PairSpliceExtend` or `KosEval_PairSpliceExtend`.

---

## 3. Opcodes with no `eval.c` static helper (typical patterns)

Many opcodes use only **stack**, **namespace**, **Python C API**, and/or **already
public** koskari APIs (`KosContext_Call`, `kos_dyn_*`, `kos_extent_*`,
`KosStackBlock_*`, `KosNamespace_*`, …) with **no** extra `static` helper from
`eval.c`. Examples: most `FAST_OBJECT_INPLACE_*`, `NOOP`, `JUMP*`, `BUILD_*`, `POP*`
(except where listed above), `KOS_OP_HANDLER` stubs that delegate to
`KosContext_Call`, etc.

---

## 4. Redundancies and consolidation targets

1. **`ensure_tuple*` family** (seven copies): single shared helper + one opcode
   table row each.
2. **`yield_from_normalize` vs `PUSH_FOREACH` acquisition** (see
   `iteration-yield-boundaries.md`): overlapping “what kind of iterable/async
   thing is this?” logic; any extracted **public** API should document semantic
   differences (yield-from vs for-each).
3. **`op_get_next_from_iter` vs `yield_from_drive_sync_iter`**: both touch sync
   iteration; consolidation must preserve YIELD_FROM vs ITER_NEXT semantics
   (exhaustion, `StopIteration` value, suspend).
4. **CHECK_* vs OBJECT_* **: parallel use of `kos_check_*` vs `op_OBJECT_EQ_impl`
   / `obj_op_*` — naming should reflect “compare for branch” vs “compare and push
   result”.
5. **Frame namespace**: `frame_ns_*` is a coherent mini-API; public names should
   stay grouped (`KosFrame_NS*` or `KosEval_FrameNS*`).

---

## 5. Migration strategy (macros to “public” names)

As discussed for the native compilation path:

1. Introduce **real** public declarations (e.g. in `eval.h` or a new
   `koskari_eval_ops.h`) with stable names such as `KosEval_GetNextFromIter`,
   implemented by the existing static functions in `eval.c`.
2. In opcode fragments (or a small included header before `handlers.incl`),
   `#define KosEval_GetNextFromIter op_get_next_from_iter` (or the reverse: define
   the public macro to the internal name for the unified TU) so **call sites
   read as the public API** while **one definition** remains optimized in
   `eval.c`.
3. After native modules link against the real symbols, gradually remove the
   macro indirection where TUs split.

This inventory should be updated when opcodes gain new helpers or when helpers
move out of `eval.c`.
