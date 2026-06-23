# Opcode VM access inventory and proposed KosContext API surface

## Document status (vs current tree)

| Section | Status | Notes |
|--------|--------|--------|
| **Part 1** | **Obsolete (historical)** | Grep snapshot from before the **`KosEvalState`** migration. It still lists **`CURRENT_STACK`**, which **no longer exists** in `koskari/lib`. Handlers today take **`KosEvalState *es`** (see `KOS_OP_HANDLER` in `koskari/lib/eval.c`). Operand stack mutations go through **`KosEvalState_Stack*`** and **`KosContext_BLOCK(ctx)`** (`ctx = KosEvalState_CONTEXT(es)`). Many opcodes still use **`KosContext *ctx`** plus **`CURRENT_CODE` / `CURRENT_FRAME` / `CURRENT_PC` / …** macros defined in `eval.c` (not `CURRENT_STACK`). **Do not use Part 1 for audits** unless you re-run an inventory. |
| **Part 2** | **Conceptual groups — naming superseded** | The **semantic surface is implemented** under **`KosEvalState_*`** in `koskari/lib/eval.h` / `eval.c`, not under the `KosContext_*` helper names used below. **`KosContext`** remains heap state with **field macros** (`KosContext_DYN_TOP`, `KosContext_FLAGS`, `KosContext_PLAN`, inject/pending-return fields, etc.). Dynamic-chain mutations from opcodes go through **`kos_dyn_*` / `kos_dyn_transition`** with a **`KosEvalState *`** (see `koskari/lib/dynchain.h`). |

**Opcode source files:** 207 under `koskari/lib/opcode/*.c` (matches Part 1 file count).

**Authoritative handler shape** (post KosOpResult migration, commit `e5c37cc`):

```c
KosOpResult op_NAME(KosEvalState *es, KosOp *insn);
```

Handlers return `EVAL_NORMAL()`, `EVAL_RAISE()`, `EVAL_YIELD(v)`,
`EVAL_AWAIT(v)`, `EVAL_RETURN(v)`, `EVAL_PARENT_RETURN(v)`, or
`EVAL_CONTROL_RETURNED()` — defined in `koskari/lib/eval.h`. The `out_ev`
parameter is gone; payloads are carried in `KosOpResult.val`. The
`KOS_OP_POST_ERR` use-counts in Part 1 are therefore stale — those sites now
use `EVAL_RAISE()` instead.

---

This document inventories **insn/operand access**, **context/frame/stack/code macros**, **structured accessors**, and **callees** inside each `KOS_OP_HANDLER` in `koskari/lib/opcode/*.c`, then proposes a consolidated VM access API (Part 2 — originally spelled as `KosContext_*`; see status table above).
Classification (for the historical Part 1 pass) excluded `CURRENT_*`, most `KosContext_*` field macros, `KosDynFrame_*`, `KosCode_*`, `KosNamespace_*`, and stack field macros `KosStackBlock_LEN` / `PARENT` / `MAX`. Call entry points in code are **`KosCallable_Call`** / **`KosCallable_CallArgs`** (not `KosContext_Call*`).

---
## Part 1 — Per-opcode inventory *(obsolete historical snapshot; see Document status)*

### OP_ATTR
**Source:** [`koskari/lib/opcode/ATTR.c`](koskari/lib/opcode/ATTR.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyObject_GetAttr`
  - `Py_DECREF`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_ATTR_CLEAR
**Source:** [`koskari/lib/opcode/ATTR_CLEAR.c`](koskari/lib/opcode/ATTR_CLEAR.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_PopRef`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyObject_DelAttr`
  - `Py_DECREF`
  - `get_name_from_insn`

### OP_ATTR_REF
**Source:** [`koskari/lib/opcode/ATTR_REF.c`](koskari/lib/opcode/ATTR_REF.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosAttributeRef_New`
  - `KosStackBlock_PopRef`
  - `Py_DECREF`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_ATTR_STORE
**Source:** [`koskari/lib/opcode/ATTR_STORE.c`](koskari/lib/opcode/ATTR_STORE.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `KOS_OP_RUN`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `PyObject_SetAttr`
  - `get_name_from_insn`

### OP_AWAIT
**Source:** [`koskari/lib/opcode/AWAIT.c`](koskari/lib/opcode/AWAIT.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`
  - `KosContext_FLAGS`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KOS_CTX_RUNNING`
  - `KOS_OP_POST_SUSPEND`
  - `KosEvent_KIND`
  - `KosEvent_PAYLOAD`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_push_int`

- **Callees**
  - `KosEvent_KIND`
  - `KosEvent_PAYLOAD`
  - `KosStackBlock_PopRef`
  - `PyErr_Format`
  - `Py_INCREF`
  - `kos_await_dispatch`
  - `kos_op_outcome_from_push_int`

### OP_BREAK
**Source:** [`koskari/lib/opcode/BREAK.c`](koskari/lib/opcode/BREAK.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PopTail`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `PyErr_SetString` (2 uses)
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PopTail`
  - `Py_DECREF`
  - `Py_INCREF`
  - `kos_dyn_transition`
  - `kos_op_outcome_from_int_step`

### OP_BUILD_ARGS
**Source:** [`koskari/lib/opcode/BUILD_ARGS.c`](koskari/lib/opcode/BUILD_ARGS.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_PopTail`
  - `KosStackBlock_PushRef`

- **Callees**
  - `Py_DECREF` (2 uses)
  - `KosStackBlock_PopTail`
  - `KosStackBlock_PushRef`

### OP_BUILD_CLASS
**Source:** [`koskari/lib/opcode/BUILD_CLASS.c`](koskari/lib/opcode/BUILD_CLASS.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (3 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (9 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `KosStackBlock_PushRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (18 uses)
  - `SAYF` (6 uses)
  - `Py_INCREF` (3 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `PyErr_SetString` (2 uses)
  - `KosStackBlock_PushRef`
  - `PyArg_ParseTuple`
  - `PyDict_Merge`
  - `PyDict_New`
  - `PyObject_Call`
  - `PySequence_Tuple`
  - `PyTuple_CheckExact`
  - `PyTuple_Pack`
  - `Py_XINCREF`
  - `kos_get_default_metaclass`
  - `kos_op_outcome_from_int_step`

### OP_BUILD_FUNCTION
**Source:** [`koskari/lib/opcode/BUILD_FUNCTION.c`](koskari/lib/opcode/BUILD_FUNCTION.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PopTailDictNames`
  - `KosStackBlock_PushRef`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `KosFunction_New`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PopTailDictNames`
  - `KosStackBlock_PushRef`
  - `Py_XDECREF`

### OP_BUILD_KWDS
**Source:** [`koskari/lib/opcode/BUILD_KWDS.c`](koskari/lib/opcode/BUILD_KWDS.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_PopTailDict`
  - `KosStackBlock_PushRef`

- **Callees**
  - `Py_DECREF` (2 uses)
  - `KosStackBlock_PopTailDict`
  - `KosStackBlock_PushRef`

### OP_BUILD_KWDS_NAMES
**Source:** [`koskari/lib/opcode/BUILD_KWDS_NAMES.c`](koskari/lib/opcode/BUILD_KWDS_NAMES.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KOS_OP_RUN`
  - `KosCode_GetConstant`
  - `KosStackBlock_PopTailDictFromNames`
  - `KosStackBlock_PushRef`

- **Callees**
  - `Py_DECREF` (2 uses)
  - `KosStackBlock_PopTailDictFromNames`
  - `KosStackBlock_PushRef`
  - `PyErr_Format`
  - `PyTuple_CheckExact`

### OP_CALL
**Source:** [`koskari/lib/opcode/CALL.c`](koskari/lib/opcode/CALL.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_FRAME`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosDynFrame_STACK` (2 uses)
  - `KosCallable_Check`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`

- **Callees**
  - `SAYF` (2 uses)
  - `KosCallable_Check`
  - `KosContext_Call`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `PyObject_CallNoArgs`
  - `PyObject_Vectorcall`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`
  - `op_push_or_await`

### OP_CALL_ATTR
**Source:** [`koskari/lib/opcode/CALL_ATTR.c`](koskari/lib/opcode/CALL_ATTR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_FRAME`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (5 uses)
  - `KosDynFrame_STACK` (2 uses)
  - `KosCallable_Check`
  - `KosCode_GetConstant`
  - `KosCode_GetExtra`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `PyObject_Vectorcall` (2 uses)
  - `KosCallable_Check`
  - `KosContext_Call`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`
  - `kos_resolve_named_attr`
  - `op_push_or_await`

### OP_CALL_ATTR_NAMES
**Source:** [`koskari/lib/opcode/CALL_ATTR_NAMES.c`](koskari/lib/opcode/CALL_ATTR_NAMES.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (7 uses)
  - `KosCode_GetConstant` (2 uses)
  - `KosCallable_Check`
  - `KosCode_GetExtra`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `PyObject_Vectorcall` (2 uses)
  - `KosCallable_Check`
  - `KosContext_Call`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `PyErr_Format`
  - `PyTuple_CheckExact`
  - `PyTuple_GET_SIZE`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`
  - `kos_resolve_named_attr`
  - `op_push_or_await`

### OP_CALL_ATTR_STAR
**Source:** [`koskari/lib/opcode/CALL_ATTR_STAR.c`](koskari/lib/opcode/CALL_ATTR_STAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (16 uses)
  - `KosStackBlock_PopRef` (8 uses)
  - `KosCode_GetConstant`
  - `KosCode_GetExtra`
  - `kos_op_outcome_from_call_int`

- **Callees**
  - `Py_DECREF` (26 uses)
  - `KosStackBlock_PopRef` (8 uses)
  - `Py_XDECREF` (4 uses)
  - `PyTuple_New` (2 uses)
  - `ensure_tuple_attr_star` (2 uses)
  - `KosContext_CallArgs`
  - `PySequence_Concat`
  - `PyTuple_Pack`
  - `kos_op_outcome_from_call_int`
  - `kos_resolve_named_attr`

### OP_CALL_FAST
**Source:** [`koskari/lib/opcode/CALL_FAST.c`](koskari/lib/opcode/CALL_FAST.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (6 uses)
  - `KosCallable_Check`
  - `KosCode_GetExtra`
  - `KosNamespace_FastDeref`
  - `KosNamespace_SupportsFast`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `KosCallable_Check`
  - `KosContext_Call`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `PyErr_Format`
  - `PyErr_SetString`
  - `PyObject_CallNoArgs`
  - `PyObject_Vectorcall`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`
  - `op_push_or_await`

### OP_CALL_FAST_ATTR
**Source:** [`koskari/lib/opcode/CALL_FAST_ATTR.c`](koskari/lib/opcode/CALL_FAST_ATTR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_FRAME`
  - `CURRENT_NS`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (9 uses)
  - `KosDynFrame_STACK` (2 uses)
  - `KosStackSteal_Free` (2 uses)
  - `KosStackSteal_TailPrepend` (2 uses)
  - `KosCallable_Check`
  - `KosCode_GetConstant`
  - `KosCode_GetExtra`
  - `KosNamespace_FastDeref`
  - `KosNamespace_SupportsFast`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `KosStackSteal_Free` (2 uses)
  - `KosStackSteal_TailPrepend` (2 uses)
  - `PyObject_Vectorcall` (2 uses)
  - `KosCallable_Check`
  - `KosContext_Call`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Tail`
  - `PyErr_Format`
  - `PyErr_SetString`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`
  - `kos_resolve_named_attr`
  - `op_push_or_await`

### OP_CALL_FAST_ATTR_NAMES
**Source:** [`koskari/lib/opcode/CALL_FAST_ATTR_NAMES.c`](koskari/lib/opcode/CALL_FAST_ATTR_NAMES.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (11 uses)
  - `KosCode_GetConstant` (2 uses)
  - `KosStackSteal_Free` (2 uses)
  - `KosStackSteal_Tail` (2 uses)
  - `KosStackSteal_TailPrepend` (2 uses)
  - `KosCallable_Check`
  - `KosCode_GetExtra`
  - `KosNamespace_FastDeref`
  - `KosNamespace_SupportsFast`
  - `KosStackBlock_TailPtr`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `KosStackSteal_Free` (2 uses)
  - `KosStackSteal_Tail` (2 uses)
  - `KosStackSteal_TailPrepend` (2 uses)
  - `PyErr_Format` (2 uses)
  - `PyObject_Vectorcall` (2 uses)
  - `KosCallable_Check`
  - `KosContext_Call`
  - `KosStackBlock_TailPtr`
  - `PyErr_SetString`
  - `PyTuple_CheckExact`
  - `PyTuple_GET_SIZE`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`
  - `kos_resolve_named_attr`
  - `op_push_or_await`

### OP_CALL_FAST_ATTR_STAR
**Source:** [`koskari/lib/opcode/CALL_FAST_ATTR_STAR.c`](koskari/lib/opcode/CALL_FAST_ATTR_STAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (15 uses)
  - `KosStackBlock_PopRef` (4 uses)
  - `KosCode_GetConstant`
  - `KosCode_GetExtra`
  - `KosNamespace_FastDeref`
  - `KosNamespace_SupportsFast`
  - `kos_op_outcome_from_call_int`

- **Callees**
  - `Py_DECREF` (14 uses)
  - `KosStackBlock_PopRef` (4 uses)
  - `Py_XDECREF` (4 uses)
  - `PyTuple_New` (2 uses)
  - `ensure_tuple_fast_attr_star` (2 uses)
  - `KosContext_CallArgs`
  - `PyErr_Format`
  - `PyErr_SetString`
  - `PySequence_Concat`
  - `PyTuple_Pack`
  - `kos_op_outcome_from_call_int`
  - `kos_resolve_named_attr`

### OP_CALL_FAST_NAMES
**Source:** [`koskari/lib/opcode/CALL_FAST_NAMES.c`](koskari/lib/opcode/CALL_FAST_NAMES.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (8 uses)
  - `KosCallable_Check`
  - `KosCode_GetConstant`
  - `KosCode_GetExtra`
  - `KosNamespace_FastDeref`
  - `KosNamespace_SupportsFast`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`

- **Callees**
  - `Py_DECREF` (6 uses)
  - `PyErr_Format` (2 uses)
  - `KosCallable_Check`
  - `KosContext_Call`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `PyErr_SetString`
  - `PyObject_Vectorcall`
  - `PyTuple_CheckExact`
  - `PyTuple_GET_SIZE`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`
  - `op_push_or_await`

### OP_CALL_FAST_STAR
**Source:** [`koskari/lib/opcode/CALL_FAST_STAR.c`](koskari/lib/opcode/CALL_FAST_STAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (11 uses)
  - `KosStackBlock_PopRef` (4 uses)
  - `KosCode_GetExtra`
  - `KosNamespace_FastDeref`
  - `KosNamespace_SupportsFast`
  - `kos_op_outcome_from_call_int`

- **Callees**
  - `Py_DECREF` (15 uses)
  - `KosStackBlock_PopRef` (4 uses)
  - `PyTuple_New` (2 uses)
  - `ensure_tuple_fast_star` (2 uses)
  - `KosContext_CallArgs`
  - `PyErr_Format`
  - `PyErr_SetString`
  - `Py_XDECREF`
  - `kos_op_outcome_from_call_int`

### OP_CALL_NAMES
**Source:** [`koskari/lib/opcode/CALL_NAMES.c`](koskari/lib/opcode/CALL_NAMES.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (5 uses)
  - `KosCallable_Check`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_Peek`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`

- **Callees**
  - `KosCallable_Check`
  - `KosContext_Call`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_Peek`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `PyErr_Format`
  - `PyObject_Vectorcall`
  - `PyTuple_CheckExact`
  - `PyTuple_GET_SIZE`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`
  - `op_push_or_await`

### OP_CALL_STAR
**Source:** [`koskari/lib/opcode/CALL_STAR.c`](koskari/lib/opcode/CALL_STAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (12 uses)
  - `KosStackBlock_PopRef` (8 uses)
  - `kos_op_outcome_from_call_int`

- **Callees**
  - `Py_DECREF` (15 uses)
  - `KosStackBlock_PopRef` (8 uses)
  - `PyTuple_New` (2 uses)
  - `ensure_tuple` (2 uses)
  - `KosContext_CallArgs`
  - `Py_XDECREF`
  - `kos_op_outcome_from_call_int`

### OP_CALL_VAR
**Source:** [`koskari/lib/opcode/CALL_VAR.c`](koskari/lib/opcode/CALL_VAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (5 uses)
  - `KosCallable_Check`
  - `KosCode_GetConstant`
  - `KosCode_GetExtra`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `SAYF` (2 uses)
  - `KosCallable_Check`
  - `KosContext_Call`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `PyObject_CallNoArgs`
  - `PyObject_Vectorcall`
  - `frame_ns_deref`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`
  - `op_push_or_await`

### OP_CALL_VAR_ATTR
**Source:** [`koskari/lib/opcode/CALL_VAR_ATTR.c`](koskari/lib/opcode/CALL_VAR_ATTR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_FRAME`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (8 uses)
  - `KosCode_GetConstant` (2 uses)
  - `KosDynFrame_STACK` (2 uses)
  - `KosStackSteal_Free` (2 uses)
  - `KosStackSteal_TailPrepend` (2 uses)
  - `KosCallable_Check`
  - `KosCode_GetExtra`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`

- **Callees**
  - `Py_DECREF` (10 uses)
  - `KosStackSteal_Free` (2 uses)
  - `KosStackSteal_TailPrepend` (2 uses)
  - `PyObject_Vectorcall` (2 uses)
  - `KosCallable_Check`
  - `KosContext_Call`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Tail`
  - `frame_ns_deref`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`
  - `kos_resolve_named_attr`
  - `op_push_or_await`

### OP_CALL_VAR_ATTR_NAMES
**Source:** [`koskari/lib/opcode/CALL_VAR_ATTR_NAMES.c`](koskari/lib/opcode/CALL_VAR_ATTR_NAMES.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (10 uses)
  - `KosCode_GetConstant` (3 uses)
  - `KosStackSteal_Free` (2 uses)
  - `KosStackSteal_TailPrepend` (2 uses)
  - `KosCallable_Check`
  - `KosCode_GetExtra`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`

- **Callees**
  - `Py_DECREF` (11 uses)
  - `KosStackSteal_Free` (2 uses)
  - `KosStackSteal_TailPrepend` (2 uses)
  - `PyObject_Vectorcall` (2 uses)
  - `KosCallable_Check`
  - `KosContext_Call`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Tail`
  - `PyErr_Format`
  - `PyTuple_CheckExact`
  - `PyTuple_GET_SIZE`
  - `frame_ns_deref`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`
  - `kos_resolve_named_attr`
  - `op_push_or_await`

### OP_CALL_VAR_ATTR_STAR
**Source:** [`koskari/lib/opcode/CALL_VAR_ATTR_STAR.c`](koskari/lib/opcode/CALL_VAR_ATTR_STAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (14 uses)
  - `KosStackBlock_PopRef` (4 uses)
  - `KosCode_GetConstant` (2 uses)
  - `KosCode_GetExtra`
  - `kos_op_outcome_from_call_int`

- **Callees**
  - `Py_DECREF` (26 uses)
  - `KosStackBlock_PopRef` (4 uses)
  - `Py_XDECREF` (4 uses)
  - `PyTuple_New` (2 uses)
  - `ensure_tuple_var_attr_star` (2 uses)
  - `KosContext_CallArgs`
  - `PySequence_Concat`
  - `PyTuple_Pack`
  - `frame_ns_deref`
  - `kos_op_outcome_from_call_int`
  - `kos_resolve_named_attr`

### OP_CALL_VAR_NAMES
**Source:** [`koskari/lib/opcode/CALL_VAR_NAMES.c`](koskari/lib/opcode/CALL_VAR_NAMES.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (7 uses)
  - `KosCode_GetConstant` (2 uses)
  - `KosCallable_Check`
  - `KosCode_GetExtra`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`

- **Callees**
  - `Py_DECREF` (6 uses)
  - `KosCallable_Check`
  - `KosContext_Call`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `PyErr_Format`
  - `PyObject_Vectorcall`
  - `PyTuple_CheckExact`
  - `PyTuple_GET_SIZE`
  - `frame_ns_deref`
  - `kos_op_outcome_from_call_int`
  - `kos_op_outcome_from_push_int`
  - `op_push_or_await`

### OP_CALL_VAR_STAR
**Source:** [`koskari/lib/opcode/CALL_VAR_STAR.c`](koskari/lib/opcode/CALL_VAR_STAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (10 uses)
  - `KosStackBlock_PopRef` (4 uses)
  - `KosCode_GetConstant`
  - `KosCode_GetExtra`
  - `kos_op_outcome_from_call_int`

- **Callees**
  - `Py_DECREF` (15 uses)
  - `KosStackBlock_PopRef` (4 uses)
  - `PyTuple_New` (2 uses)
  - `ensure_tuple_var_star` (2 uses)
  - `KosContext_CallArgs`
  - `Py_XDECREF`
  - `frame_ns_deref`
  - `kos_op_outcome_from_call_int`

### OP_CAPTURE_CC
**Source:** [`koskari/lib/opcode/CAPTURE_CC.c`](koskari/lib/opcode/CAPTURE_CC.c)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosPyContinuation_New`
  - `kos_capture_cc`
  - `kos_continuation_free`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_CAR
**Source:** [`koskari/lib/opcode/CAR.c`](koskari/lib/opcode/CAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosNil_Check` (2 uses)
  - `PyErr_Format` (2 uses)
  - `Py_DECREF` (2 uses)
  - `KosPair_CAR`
  - `KosPair_Check`
  - `KosStackBlock_PopRef`
  - `Py_INCREF`
  - `Py_TYPE`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_CDR
**Source:** [`koskari/lib/opcode/CDR.c`](koskari/lib/opcode/CDR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosNil_Check` (2 uses)
  - `PyErr_Format` (2 uses)
  - `Py_DECREF` (2 uses)
  - `KosPair_CDR`
  - `KosPair_Check`
  - `KosStackBlock_PopRef`
  - `Py_INCREF`
  - `Py_TYPE`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_CHECK_EQ
**Source:** [`koskari/lib/opcode/CHECK_EQ.c`](koskari/lib/opcode/CHECK_EQ.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_check_cmp`
  - `kos_op_outcome_from_int_step`

### OP_CHECK_EXCEPTION
**Source:** [`koskari/lib/opcode/CHECK_EXCEPTION.c`](koskari/lib/opcode/CHECK_EXCEPTION.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_PC`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR`
  - `KOS_OP_RUN`
  - `KosStackBlock_PopRef`

- **Callees**
  - `Py_XDECREF` (3 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_GetExcInfo`
  - `PyErr_GivenExceptionMatches`
  - `Py_DECREF`

### OP_CHECK_GE
**Source:** [`koskari/lib/opcode/CHECK_GE.c`](koskari/lib/opcode/CHECK_GE.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_check_cmp`
  - `kos_op_outcome_from_int_step`

### OP_CHECK_GT
**Source:** [`koskari/lib/opcode/CHECK_GT.c`](koskari/lib/opcode/CHECK_GT.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_check_cmp`
  - `kos_op_outcome_from_int_step`

### OP_CHECK_IN
**Source:** [`koskari/lib/opcode/CHECK_IN.c`](koskari/lib/opcode/CHECK_IN.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_check_contains`
  - `kos_op_outcome_from_int_step`

### OP_CHECK_IS
**Source:** [`koskari/lib/opcode/CHECK_IS.c`](koskari/lib/opcode/CHECK_IS.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_check_identity`
  - `kos_op_outcome_from_int_step`

### OP_CHECK_IS_INSTANCE
**Source:** [`koskari/lib/opcode/CHECK_IS_INSTANCE.c`](koskari/lib/opcode/CHECK_IS_INSTANCE.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_check_two_arg`
  - `kos_op_outcome_from_int_step`

### OP_CHECK_IS_NOT
**Source:** [`koskari/lib/opcode/CHECK_IS_NOT.c`](koskari/lib/opcode/CHECK_IS_NOT.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_check_identity`
  - `kos_op_outcome_from_int_step`

### OP_CHECK_IS_SUBCLASS
**Source:** [`koskari/lib/opcode/CHECK_IS_SUBCLASS.c`](koskari/lib/opcode/CHECK_IS_SUBCLASS.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_check_two_arg`
  - `kos_op_outcome_from_int_step`

### OP_CHECK_LE
**Source:** [`koskari/lib/opcode/CHECK_LE.c`](koskari/lib/opcode/CHECK_LE.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_check_cmp`
  - `kos_op_outcome_from_int_step`

### OP_CHECK_LT
**Source:** [`koskari/lib/opcode/CHECK_LT.c`](koskari/lib/opcode/CHECK_LT.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_check_cmp`
  - `kos_op_outcome_from_int_step`

### OP_CHECK_NE
**Source:** [`koskari/lib/opcode/CHECK_NE.c`](koskari/lib/opcode/CHECK_NE.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_check_cmp`
  - `kos_op_outcome_from_int_step`

### OP_CHECK_NOT_IN
**Source:** [`koskari/lib/opcode/CHECK_NOT_IN.c`](koskari/lib/opcode/CHECK_NOT_IN.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_check_contains`
  - `kos_op_outcome_from_int_step`

### OP_CLEAR_FINALLY
**Source:** [`koskari/lib/opcode/CLEAR_FINALLY.c`](koskari/lib/opcode/CLEAR_FINALLY.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `KosContext_DYN_TOP`

- **Structured accessors and outcome macros**
  - `KosDynNode_IS_FINALLY` (2 uses)
  - `KOS_OP_POST_ERR`
  - `KOS_OP_RUN`
  - `KosDynFinally_ENABLED`

- **Callees**
  - `KosDynFinally_ENABLED`
  - `KosDynNode_IS_FINALLY`
  - `PyErr_SetString`

### OP_CLEAR_FOREACH
**Source:** [`koskari/lib/opcode/CLEAR_FOREACH.c`](koskari/lib/opcode/CLEAR_FOREACH.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`
  - `KosContext_DYN_TOP`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosDynNode_IS_FOREACH` (2 uses)
  - `KOS_OP_RUN`
  - `KosDynForeach_ENABLED`
  - `KosStackBlock_ClearBlock`

- **Callees**
  - `KosDynForeach_ENABLED`
  - `KosDynNode_IS_FOREACH`
  - `KosStackBlock_ClearBlock`
  - `PyErr_SetString`

### OP_CLEAR_LOOP
**Source:** [`koskari/lib/opcode/CLEAR_LOOP.c`](koskari/lib/opcode/CLEAR_LOOP.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`
  - `KosContext_DYN_TOP`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosDynNode_IS_LOOP` (2 uses)
  - `KOS_OP_RUN`
  - `KosDynLoop_ENABLED`
  - `KosStackBlock_ClearBlock`

- **Callees**
  - `KosDynLoop_ENABLED`
  - `KosDynNode_IS_LOOP`
  - `KosStackBlock_ClearBlock`
  - `PyErr_SetString`

### OP_CLEAR_TRY
**Source:** [`koskari/lib/opcode/CLEAR_TRY.c`](koskari/lib/opcode/CLEAR_TRY.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `KosContext_DYN_TOP`

- **Structured accessors and outcome macros**
  - `KosDynNode_IS_TRY` (2 uses)
  - `KOS_OP_POST_ERR`
  - `KOS_OP_RUN`
  - `KosDynTry_ENABLED`

- **Callees**
  - `KosDynNode_IS_TRY`
  - `KosDynTry_ENABLED`
  - `PyErr_SetString`

### OP_COMBINE_ARGS
**Source:** [`koskari/lib/opcode/COMBINE_ARGS.c`](koskari/lib/opcode/COMBINE_ARGS.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (7 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (10 uses)
  - `KOS_OP_RUN` (3 uses)
  - `KosStackBlock_PushRef` (3 uses)
  - `KosStackBlock_DropNoErr` (2 uses)
  - `KosStackBlock_PopRef`
  - `KosStackBlock_TailPtr`

- **Callees**
  - `Py_DECREF` (9 uses)
  - `KosStackBlock_PushRef` (3 uses)
  - `KosStackBlock_DropNoErr` (2 uses)
  - `PySequence_Tuple` (2 uses)
  - `KosStackBlock_PopRef`
  - `KosStackBlock_TailPtr`
  - `PyList_New`
  - `PySequence_InPlaceConcat`
  - `PyTuple_CheckExact`
  - `PyTuple_New`

### OP_COMBINE_KWDS
**Source:** [`koskari/lib/opcode/COMBINE_KWDS.c`](koskari/lib/opcode/COMBINE_KWDS.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (5 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (5 uses)
  - `KOS_OP_RUN` (2 uses)
  - `KosStackBlock_DropNoErr` (2 uses)
  - `KosStackBlock_PushRef` (2 uses)
  - `KosStackBlock_TailPtr`

- **Callees**
  - `Py_DECREF` (6 uses)
  - `KosStackBlock_DropNoErr` (2 uses)
  - `KosStackBlock_PushRef` (2 uses)
  - `KosStackBlock_TailPtr`
  - `PyDict_Merge`
  - `PyDict_New`

### OP_CONS
**Source:** [`koskari/lib/opcode/CONS.c`](koskari/lib/opcode/CONS.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `kos_op_outcome_from_int_step` (2 uses)
  - `KosStackBlock_Drop`
  - `KosStackBlock_TailPtr`

- **Callees**
  - `kos_op_outcome_from_int_step` (2 uses)
  - `push_result` (2 uses)
  - `KosPair_ConsFromPtr`
  - `KosStackBlock_Drop`
  - `KosStackBlock_TailPtr`
  - `Py_DECREF`
  - `Py_INCREF`

### OP_CONST
**Source:** [`koskari/lib/opcode/CONST.c`](koskari/lib/opcode/CONST.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR`
  - `KosCode_CPOOL_LEN`
  - `KosCode_GET_CONST`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `PyErr_Format`
  - `Py_INCREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`
  - `unlikely`

### OP_CONST_ITEM
**Source:** [`koskari/lib/opcode/CONST_ITEM.c`](koskari/lib/opcode/CONST_ITEM.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosCode_GetConstant`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyObject_GetItem`
  - `Py_DECREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_CONST_ITEM_CLEAR
**Source:** [`koskari/lib/opcode/CONST_ITEM_CLEAR.c`](koskari/lib/opcode/CONST_ITEM_CLEAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KOS_OP_RUN`
  - `KosCode_GetConstant`
  - `KosStackBlock_PopRef`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyObject_DelItem`
  - `Py_DECREF`

### OP_CONST_ITEM_REF
**Source:** [`koskari/lib/opcode/CONST_ITEM_REF.c`](koskari/lib/opcode/CONST_ITEM_REF.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosCode_GetConstant`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosItemRef_New`
  - `KosStackBlock_PopRef`
  - `Py_DECREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_CONST_ITEM_STORE
**Source:** [`koskari/lib/opcode/CONST_ITEM_STORE.c`](koskari/lib/opcode/CONST_ITEM_STORE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `KOS_OP_RUN`
  - `KosCode_GetConstant`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `PyObject_SetItem`

### OP_CONTINUE
**Source:** [`koskari/lib/opcode/CONTINUE.c`](koskari/lib/opcode/CONTINUE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PopTail`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `PyErr_SetString` (2 uses)
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PopTail`
  - `Py_DECREF`
  - `Py_INCREF`
  - `kos_dyn_transition`
  - `kos_op_outcome_from_int_step`

### OP_DEREF
**Source:** [`koskari/lib/opcode/DEREF.c`](koskari/lib/opcode/DEREF.c)

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyObject_CallMethod`
  - `Py_DECREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_DUP
**Source:** [`koskari/lib/opcode/DUP.c`](koskari/lib/opcode/DUP.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_Duplicate`

- **Callees**
  - `KosStackBlock_Duplicate`
  - `PyErr_Format`

### OP_EXCEPTION
**Source:** [`koskari/lib/opcode/EXCEPTION.c`](koskari/lib/opcode/EXCEPTION.c)

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step` (2 uses)

- **Callees**
  - `kos_op_outcome_from_int_step` (2 uses)
  - `push_result` (2 uses)
  - `PyErr_GetExcInfo`
  - `PyErr_NormalizeException`
  - `Py_DECREF`
  - `Py_INCREF`
  - `Py_XDECREF`

### OP_EXCEPTION_CLEAR
**Source:** [`koskari/lib/opcode/EXCEPTION_CLEAR.c`](koskari/lib/opcode/EXCEPTION_CLEAR.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `KOS_OP_RUN`

- **Callees**
  - `PyErr_Clear`
  - `PyErr_SetExcInfo`

### OP_EXCEPTION_MATCH
**Source:** [`koskari/lib/opcode/EXCEPTION_MATCH.c`](koskari/lib/opcode/EXCEPTION_MATCH.c)

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_XDECREF` (3 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_GetExcInfo`
  - `PyErr_GivenExceptionMatches`
  - `Py_DECREF`
  - `Py_INCREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_FAST
**Source:** [`koskari/lib/opcode/FAST.c`](koskari/lib/opcode/FAST.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosNamespace_FastDeref`
  - `KosNamespace_SupportsFast`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `PyErr_Format`
  - `PyErr_SetString`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_FAST_ATTR
**Source:** [`koskari/lib/opcode/FAST_ATTR.c`](koskari/lib/opcode/FAST_ATTR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_NS`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (5 uses)
  - `KosCode_GetConstant`
  - `KosCode_GetExtra`
  - `KosNamespace_FastDeref`
  - `KosNamespace_SupportsFast`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `PyErr_Format`
  - `PyErr_SetString`
  - `PyObject_GetAttr`
  - `Py_DECREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_FAST_ATTR_CLEAR
**Source:** [`koskari/lib/opcode/FAST_ATTR_CLEAR.c`](koskari/lib/opcode/FAST_ATTR_CLEAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_NS`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (5 uses)
  - `KOS_OP_RUN`
  - `KosCode_GetConstant`
  - `KosCode_GetExtra`
  - `KosNamespace_FastDeref`
  - `KosNamespace_SupportsFast`

- **Callees**
  - `PyErr_Format`
  - `PyErr_SetString`
  - `PyObject_DelAttr`
  - `Py_DECREF`

### OP_FAST_ATTR_REF
**Source:** [`koskari/lib/opcode/FAST_ATTR_REF.c`](koskari/lib/opcode/FAST_ATTR_REF.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_NS`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (5 uses)
  - `KosCode_GetConstant`
  - `KosCode_GetExtra`
  - `KosNamespace_FastDeref`
  - `KosNamespace_SupportsFast`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosAttributeRef_New`
  - `PyErr_Format`
  - `PyErr_SetString`
  - `Py_DECREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_FAST_ATTR_STORE
**Source:** [`koskari/lib/opcode/FAST_ATTR_STORE.c`](koskari/lib/opcode/FAST_ATTR_STORE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (6 uses)
  - `KOS_OP_RUN`
  - `KosCode_GetConstant`
  - `KosCode_GetExtra`
  - `KosNamespace_FastDeref`
  - `KosNamespace_SupportsFast`
  - `KosStackBlock_PopRef`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_Format`
  - `PyErr_SetString`
  - `PyObject_SetAttr`

### OP_FAST_BIND
**Source:** [`koskari/lib/opcode/FAST_BIND.c`](koskari/lib/opcode/FAST_BIND.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosNamespace_FAST_COUNT`
  - `KosNamespace_FastBind`
  - `KosNamespace_SupportsFast`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `unlikely` (4 uses)
  - `PyErr_Format` (2 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `Py_DECREF`
  - `kos_op_outcome_from_int_step`

### OP_FAST_CLEAR
**Source:** [`koskari/lib/opcode/FAST_CLEAR.c`](koskari/lib/opcode/FAST_CLEAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KOS_OP_RUN`
  - `KosNamespace_FastClear`
  - `KosNamespace_SupportsFast`

- **Callees**
  - `PyErr_Format`
  - `PyErr_SetString`

### OP_FAST_OBJECT_ADD
**Source:** [`koskari/lib/opcode/FAST_OBJECT_ADD.c`](koskari/lib/opcode/FAST_OBJECT_ADD.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosNamespace_FastDeref`
  - `KosNamespace_FastStore`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `PyNumber_InPlaceAdd`
  - `kos_op_outcome_from_int_step`

### OP_FAST_OBJECT_BIT_AND
**Source:** [`koskari/lib/opcode/FAST_OBJECT_BIT_AND.c`](koskari/lib/opcode/FAST_OBJECT_BIT_AND.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosNamespace_FastDeref`
  - `KosNamespace_FastStore`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `PyNumber_InPlaceAnd`
  - `kos_op_outcome_from_int_step`

### OP_FAST_OBJECT_BIT_LEFT
**Source:** [`koskari/lib/opcode/FAST_OBJECT_BIT_LEFT.c`](koskari/lib/opcode/FAST_OBJECT_BIT_LEFT.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosNamespace_FastDeref`
  - `KosNamespace_FastStore`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `PyNumber_InPlaceLshift`
  - `kos_op_outcome_from_int_step`

### OP_FAST_OBJECT_BIT_OR
**Source:** [`koskari/lib/opcode/FAST_OBJECT_BIT_OR.c`](koskari/lib/opcode/FAST_OBJECT_BIT_OR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosNamespace_FastDeref`
  - `KosNamespace_FastStore`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `PyNumber_InPlaceOr`
  - `kos_op_outcome_from_int_step`

### OP_FAST_OBJECT_BIT_RIGHT
**Source:** [`koskari/lib/opcode/FAST_OBJECT_BIT_RIGHT.c`](koskari/lib/opcode/FAST_OBJECT_BIT_RIGHT.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosNamespace_FastDeref`
  - `KosNamespace_FastStore`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `PyNumber_InPlaceRshift`
  - `kos_op_outcome_from_int_step`

### OP_FAST_OBJECT_BIT_XOR
**Source:** [`koskari/lib/opcode/FAST_OBJECT_BIT_XOR.c`](koskari/lib/opcode/FAST_OBJECT_BIT_XOR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosNamespace_FastDeref`
  - `KosNamespace_FastStore`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `PyNumber_InPlaceXor`
  - `kos_op_outcome_from_int_step`

### OP_FAST_OBJECT_FLOOR_DIVIDE
**Source:** [`koskari/lib/opcode/FAST_OBJECT_FLOOR_DIVIDE.c`](koskari/lib/opcode/FAST_OBJECT_FLOOR_DIVIDE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosNamespace_FastDeref`
  - `KosNamespace_FastStore`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `PyNumber_InPlaceFloorDivide`
  - `kos_op_outcome_from_int_step`

### OP_FAST_OBJECT_MATRIX_MULTIPLY
**Source:** [`koskari/lib/opcode/FAST_OBJECT_MATRIX_MULTIPLY.c`](koskari/lib/opcode/FAST_OBJECT_MATRIX_MULTIPLY.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosNamespace_FastDeref`
  - `KosNamespace_FastStore`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `PyNumber_InPlaceMatrixMultiply`
  - `kos_op_outcome_from_int_step`

### OP_FAST_OBJECT_MODULO
**Source:** [`koskari/lib/opcode/FAST_OBJECT_MODULO.c`](koskari/lib/opcode/FAST_OBJECT_MODULO.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosNamespace_FastDeref`
  - `KosNamespace_FastStore`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `PyNumber_InPlaceRemainder`
  - `kos_op_outcome_from_int_step`

### OP_FAST_OBJECT_MULTIPLY
**Source:** [`koskari/lib/opcode/FAST_OBJECT_MULTIPLY.c`](koskari/lib/opcode/FAST_OBJECT_MULTIPLY.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosNamespace_FastDeref`
  - `KosNamespace_FastStore`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `PyNumber_InPlaceMultiply`
  - `kos_op_outcome_from_int_step`

### OP_FAST_OBJECT_POWER
**Source:** [`koskari/lib/opcode/FAST_OBJECT_POWER.c`](koskari/lib/opcode/FAST_OBJECT_POWER.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosNamespace_FastDeref`
  - `KosNamespace_FastStore`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `PyNumber_InPlacePower`
  - `kos_op_outcome_from_int_step`

### OP_FAST_OBJECT_SUBTRACT
**Source:** [`koskari/lib/opcode/FAST_OBJECT_SUBTRACT.c`](koskari/lib/opcode/FAST_OBJECT_SUBTRACT.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosNamespace_FastDeref`
  - `KosNamespace_FastStore`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `PyNumber_InPlaceSubtract`
  - `kos_op_outcome_from_int_step`

### OP_FAST_OBJECT_TRUE_DIVIDE
**Source:** [`koskari/lib/opcode/FAST_OBJECT_TRUE_DIVIDE.c`](koskari/lib/opcode/FAST_OBJECT_TRUE_DIVIDE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosNamespace_FastDeref`
  - `KosNamespace_FastStore`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `PyNumber_InPlaceTrueDivide`
  - `kos_op_outcome_from_int_step`

### OP_FAST_REF
**Source:** [`koskari/lib/opcode/FAST_REF.c`](koskari/lib/opcode/FAST_REF.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosNamespace_FastRef`
  - `KosNamespace_SupportsFast`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `PyErr_Format`
  - `PyErr_SetString`
  - `Py_INCREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_FAST_STORE
**Source:** [`koskari/lib/opcode/FAST_STORE.c`](koskari/lib/opcode/FAST_STORE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KOS_OP_RUN`
  - `KosNamespace_FastStore`
  - `KosNamespace_SupportsFast`
  - `KosStackBlock_PopRef`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyErr_Format`
  - `PyErr_SetString`
  - `Py_DECREF`

### OP_GLOBALS
**Source:** [`koskari/lib/opcode/GLOBALS.c`](koskari/lib/opcode/GLOBALS.c)

- **Context / frame / stack / code macros**
  - `KosContext_GLOBALS_FRAME`

- **Structured accessors and outcome macros**
  - `KosDynFrame_NS` (2 uses)
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_INCREF`
  - `kos_dyn_globals_frame`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_IMPORT
**Source:** [`koskari/lib/opcode/IMPORT.c`](koskari/lib/opcode/IMPORT.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `PyImport_ImportModuleLevelObject`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_IMPORT_FROM
**Source:** [`koskari/lib/opcode/IMPORT_FROM.c`](koskari/lib/opcode/IMPORT_FROM.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_Peek`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_Peek`
  - `PyObject_GetAttr`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_IMPORT_REL
**Source:** [`koskari/lib/opcode/IMPORT_REL.c`](koskari/lib/opcode/IMPORT_REL.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `KosContext_GLOBALS_FRAME`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (8 uses)
  - `KosDynFrame_NS` (2 uses)
  - `KosNamespace_Deref` (2 uses)
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (9 uses)
  - `PyDict_SetItem` (2 uses)
  - `PyTuple_GET_ITEM` (2 uses)
  - `PyDict_New`
  - `PyErr_Occurred`
  - `PyErr_SetString`
  - `PyImport_ImportModuleLevelObject`
  - `PyLong_AsLong`
  - `PyTuple_Check`
  - `PyTuple_GET_SIZE`
  - `get_name_from_insn`
  - `kos_dyn_globals_frame`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_ITEM
**Source:** [`koskari/lib/opcode/ITEM.c`](koskari/lib/opcode/ITEM.c)

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `PyObject_GetItem`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_ITEM_CLEAR
**Source:** [`koskari/lib/opcode/ITEM_CLEAR.c`](koskari/lib/opcode/ITEM_CLEAR.c)

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `KOS_OP_RUN`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `PyObject_DelItem`

### OP_ITEM_REF
**Source:** [`koskari/lib/opcode/ITEM_REF.c`](koskari/lib/opcode/ITEM_REF.c)

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `KosItemRef_New`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_ITEM_STORE
**Source:** [`koskari/lib/opcode/ITEM_STORE.c`](koskari/lib/opcode/ITEM_STORE.c)

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KOS_OP_RUN`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `KOS_STACK_BLOCK_POP_ARGS`
  - `PyObject_SetItem`

### OP_ITER_NEXT
**Source:** [`koskari/lib/opcode/ITER_NEXT.c`](koskari/lib/opcode/ITER_NEXT.c)

- **Insn / operand**
  - `KosOp_ARG` (3 uses)
  - `insn` (3 uses)

- **Context / frame / stack / code macros**
  - `CURRENT_PC` (2 uses)
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (6 uses)
  - `KOS_OP_RUN` (4 uses)
  - `KosDynForeach_EXHAUSTED` (2 uses)
  - `KosDynForeach_ITER_STEP_PHASE` (2 uses)
  - `KosDynNode_IS_FOREACH` (2 uses)
  - `KOS_OP_POST_SUSPEND`
  - `KosDynForeach_ITER`
  - `KosDynForeach_ITER_NEXT_ELSE_PC`
  - `KosStackBlock_PushRef`

- **Callees**
  - `KosDynForeach_EXHAUSTED` (2 uses)
  - `KosDynForeach_ITER_STEP_PHASE` (2 uses)
  - `PyErr_SetString` (2 uses)
  - `Py_DECREF` (2 uses)
  - `KosDynForeach_ITER`
  - `KosDynForeach_ITER_NEXT_ELSE_PC`
  - `KosDynNode_IS_FOREACH`
  - `KosPyAsyncGen_Check`
  - `KosStackBlock_PushRef`
  - `kos_find_node_for_loop`
  - `op_attach_koskari_agen_frame_for_iter_next`
  - `op_get_next_from_iter`

### OP_JUMP
**Source:** [`koskari/lib/opcode/JUMP.c`](koskari/lib/opcode/JUMP.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_PC`

- **Structured accessors and outcome macros**
  - `KOS_OP_RUN`

### OP_JUMP_AND
**Source:** [`koskari/lib/opcode/JUMP_AND.c`](koskari/lib/opcode/JUMP_AND.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)
  - `CURRENT_PC`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushRef`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushRef`
  - `PyObject_Not`

### OP_JUMP_FALSE
**Source:** [`koskari/lib/opcode/JUMP_FALSE.c`](koskari/lib/opcode/JUMP_FALSE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_PC`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_PopRef`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyObject_Not`
  - `Py_DECREF`

### OP_JUMP_OR
**Source:** [`koskari/lib/opcode/JUMP_OR.c`](koskari/lib/opcode/JUMP_OR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)
  - `CURRENT_PC`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushRef`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushRef`
  - `PyObject_IsTrue`

### OP_JUMP_TRUE
**Source:** [`koskari/lib/opcode/JUMP_TRUE.c`](koskari/lib/opcode/JUMP_TRUE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_PC`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_PopRef`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyObject_IsTrue`
  - `Py_DECREF`

### OP_LABEL
**Source:** [`koskari/lib/opcode/LABEL.c`](koskari/lib/opcode/LABEL.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `KOS_OP_RUN`

### OP_LOCALS
**Source:** [`koskari/lib/opcode/LOCALS.c`](koskari/lib/opcode/LOCALS.c)

- **Context / frame / stack / code macros**
  - `CURRENT_NS`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_INCREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_NEXT
**Source:** [`koskari/lib/opcode/NEXT.c`](koskari/lib/opcode/NEXT.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (8 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (15 uses)
  - `KosStackBlock_PushRef` (5 uses)
  - `KOS_OP_RUN` (4 uses)
  - `KosStackBlock_PopRef` (3 uses)
  - `KOS_OP_POST_SUSPEND`

- **Callees**
  - `Py_DECREF` (16 uses)
  - `Py_XDECREF` (16 uses)
  - `KosStackBlock_PushRef` (5 uses)
  - `KosStackBlock_PopRef` (3 uses)
  - `PyErr_SetNone` (2 uses)
  - `KosPyAsyncGen_Check`
  - `PyErr_Format`
  - `PyErr_SetString`
  - `kos_dyn_push_next_for_kos_agen`
  - `op_attach_koskari_agen_frame`
  - `op_get_next_from_iter`

### OP_NOOP
**Source:** [`koskari/lib/opcode/NOOP.c`](koskari/lib/opcode/NOOP.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `KOS_OP_RUN`

### OP_OBJECT_ADD
**Source:** [`koskari/lib/opcode/OBJECT_ADD.c`](koskari/lib/opcode/OBJECT_ADD.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_reducing`

### OP_OBJECT_AITER
**Source:** [`koskari/lib/opcode/OBJECT_AITER.c`](koskari/lib/opcode/OBJECT_AITER.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (2 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_Format`
  - `Py_TYPE`
  - `am_aiter`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_BIT_AND
**Source:** [`koskari/lib/opcode/OBJECT_BIT_AND.c`](koskari/lib/opcode/OBJECT_BIT_AND.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_reducing`

### OP_OBJECT_BIT_INVERT
**Source:** [`koskari/lib/opcode/OBJECT_BIT_INVERT.c`](koskari/lib/opcode/OBJECT_BIT_INVERT.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyNumber_Invert`
  - `Py_DECREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_BIT_LEFT
**Source:** [`koskari/lib/opcode/OBJECT_BIT_LEFT.c`](koskari/lib/opcode/OBJECT_BIT_LEFT.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_binary`

### OP_OBJECT_BIT_OR
**Source:** [`koskari/lib/opcode/OBJECT_BIT_OR.c`](koskari/lib/opcode/OBJECT_BIT_OR.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_reducing`

### OP_OBJECT_BIT_RIGHT
**Source:** [`koskari/lib/opcode/OBJECT_BIT_RIGHT.c`](koskari/lib/opcode/OBJECT_BIT_RIGHT.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_binary`

### OP_OBJECT_BIT_XOR
**Source:** [`koskari/lib/opcode/OBJECT_BIT_XOR.c`](koskari/lib/opcode/OBJECT_BIT_XOR.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_reducing`

### OP_OBJECT_DIR
**Source:** [`koskari/lib/opcode/OBJECT_DIR.c`](koskari/lib/opcode/OBJECT_DIR.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyObject_Dir`
  - `Py_DECREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_DIVMOD
**Source:** [`koskari/lib/opcode/OBJECT_DIVMOD.c`](koskari/lib/opcode/OBJECT_DIVMOD.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `PyErr_SetString`
  - `PyNumber_Divmod`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_EQ
**Source:** [`koskari/lib/opcode/OBJECT_EQ.c`](koskari/lib/opcode/OBJECT_EQ.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `unlikely` (2 uses)
  - `KosOperator_Eq`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `PyErr_Format`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_FLOOR_DIVIDE
**Source:** [`koskari/lib/opcode/OBJECT_FLOOR_DIVIDE.c`](koskari/lib/opcode/OBJECT_FLOOR_DIVIDE.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_reducing`

### OP_OBJECT_FORMAT
**Source:** [`koskari/lib/opcode/OBJECT_FORMAT.c`](koskari/lib/opcode/OBJECT_FORMAT.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef` (2 uses)
  - `Py_XDECREF` (2 uses)
  - `PyErr_SetString`
  - `PyObject_Format`
  - `Py_DECREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_FORMAT_CONST
**Source:** [`koskari/lib/opcode/OBJECT_FORMAT_CONST.c`](koskari/lib/opcode/OBJECT_FORMAT_CONST.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosCode_GetConstant`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyObject_Format`
  - `Py_DECREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_GE
**Source:** [`koskari/lib/opcode/OBJECT_GE.c`](koskari/lib/opcode/OBJECT_GE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `unlikely` (2 uses)
  - `KosOperator_Ge`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `PyErr_Format`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_GT
**Source:** [`koskari/lib/opcode/OBJECT_GT.c`](koskari/lib/opcode/OBJECT_GT.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `unlikely` (2 uses)
  - `KosOperator_Gt`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `PyErr_Format`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_IN
**Source:** [`koskari/lib/opcode/OBJECT_IN.c`](koskari/lib/opcode/OBJECT_IN.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_binary`

### OP_OBJECT_IS
**Source:** [`koskari/lib/opcode/OBJECT_IS.c`](koskari/lib/opcode/OBJECT_IS.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_binary`

### OP_OBJECT_IS_INSTANCE
**Source:** [`koskari/lib/opcode/OBJECT_IS_INSTANCE.c`](koskari/lib/opcode/OBJECT_IS_INSTANCE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (4 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (5 uses)
  - `KosStackBlock_PopRef` (3 uses)
  - `KosStackBlock_PopTail`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `KosStackBlock_PopRef` (3 uses)
  - `KosStackBlock_PopTail`
  - `PyObject_IsInstance`
  - `Py_INCREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_IS_NOT
**Source:** [`koskari/lib/opcode/OBJECT_IS_NOT.c`](koskari/lib/opcode/OBJECT_IS_NOT.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_binary`

### OP_OBJECT_IS_SUBCLASS
**Source:** [`koskari/lib/opcode/OBJECT_IS_SUBCLASS.c`](koskari/lib/opcode/OBJECT_IS_SUBCLASS.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (4 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (5 uses)
  - `KosStackBlock_PopRef` (3 uses)
  - `KosStackBlock_PopTail`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `KosStackBlock_PopRef` (3 uses)
  - `KosStackBlock_PopTail`
  - `PyObject_IsSubclass`
  - `Py_INCREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_ITER
**Source:** [`koskari/lib/opcode/OBJECT_ITER.c`](koskari/lib/opcode/OBJECT_ITER.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyObject_GetIter`
  - `Py_DECREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_LE
**Source:** [`koskari/lib/opcode/OBJECT_LE.c`](koskari/lib/opcode/OBJECT_LE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `unlikely` (2 uses)
  - `KosOperator_Le`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `PyErr_Format`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_LEN
**Source:** [`koskari/lib/opcode/OBJECT_LEN.c`](koskari/lib/opcode/OBJECT_LEN.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyLong_FromSsize_t`
  - `PyObject_Length`
  - `Py_DECREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_LT
**Source:** [`koskari/lib/opcode/OBJECT_LT.c`](koskari/lib/opcode/OBJECT_LT.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `unlikely` (2 uses)
  - `KosOperator_Lt`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `PyErr_Format`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_MATRIX_MULTIPLY
**Source:** [`koskari/lib/opcode/OBJECT_MATRIX_MULTIPLY.c`](koskari/lib/opcode/OBJECT_MATRIX_MULTIPLY.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_reducing`

### OP_OBJECT_MODULO
**Source:** [`koskari/lib/opcode/OBJECT_MODULO.c`](koskari/lib/opcode/OBJECT_MODULO.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `PyErr_SetString`
  - `PyNumber_Remainder`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_MULTIPLY
**Source:** [`koskari/lib/opcode/OBJECT_MULTIPLY.c`](koskari/lib/opcode/OBJECT_MULTIPLY.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_reducing`

### OP_OBJECT_NE
**Source:** [`koskari/lib/opcode/OBJECT_NE.c`](koskari/lib/opcode/OBJECT_NE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `unlikely` (2 uses)
  - `KosOperator_Ne`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `PyErr_Format`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_NOT
**Source:** [`koskari/lib/opcode/OBJECT_NOT.c`](koskari/lib/opcode/OBJECT_NOT.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyObject_Not`
  - `Py_DECREF`
  - `Py_INCREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_NOT_IN
**Source:** [`koskari/lib/opcode/OBJECT_NOT_IN.c`](koskari/lib/opcode/OBJECT_NOT_IN.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_binary`

### OP_OBJECT_POWER
**Source:** [`koskari/lib/opcode/OBJECT_POWER.c`](koskari/lib/opcode/OBJECT_POWER.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (3 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (5 uses)
  - `KosStackBlock_PopRef` (3 uses)
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (6 uses)
  - `KosStackBlock_PopRef` (3 uses)
  - `PyErr_Format`
  - `PyNumber_Power`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_REPR
**Source:** [`koskari/lib/opcode/OBJECT_REPR.c`](koskari/lib/opcode/OBJECT_REPR.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyObject_Repr`
  - `Py_DECREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_STR
**Source:** [`koskari/lib/opcode/OBJECT_STR.c`](koskari/lib/opcode/OBJECT_STR.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_reducing`

### OP_OBJECT_SUBTRACT
**Source:** [`koskari/lib/opcode/OBJECT_SUBTRACT.c`](koskari/lib/opcode/OBJECT_SUBTRACT.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_reducing`

### OP_OBJECT_TRUE_DIVIDE
**Source:** [`koskari/lib/opcode/OBJECT_TRUE_DIVIDE.c`](koskari/lib/opcode/OBJECT_TRUE_DIVIDE.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `kos_op_outcome_from_int_step`
  - `obj_op_reducing`

### OP_OBJECT_TRUTH
**Source:** [`koskari/lib/opcode/OBJECT_TRUTH.c`](koskari/lib/opcode/OBJECT_TRUTH.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyObject_IsTrue`
  - `Py_DECREF`
  - `Py_INCREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_TYPE
**Source:** [`koskari/lib/opcode/OBJECT_TYPE.c`](koskari/lib/opcode/OBJECT_TYPE.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR`
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef`
  - `Py_DECREF`
  - `Py_INCREF`
  - `Py_TYPE`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_OBJECT_VARS
**Source:** [`koskari/lib/opcode/OBJECT_VARS.c`](koskari/lib/opcode/OBJECT_VARS.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef`
  - `PyErr_Clear`
  - `PyErr_SetString`
  - `PyObject_GetAttrString`
  - `Py_DECREF`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_PAIR_SPLICE
**Source:** [`koskari/lib/opcode/PAIR_SPLICE.c`](koskari/lib/opcode/PAIR_SPLICE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (6 uses)
  - `kos_op_outcome_from_int_step` (2 uses)
  - `KosStackBlock_Drop`
  - `KosStackBlock_TailPtr`

- **Callees**
  - `Py_DECREF` (5 uses)
  - `Py_INCREF` (2 uses)
  - `kos_op_outcome_from_int_step` (2 uses)
  - `push_result` (2 uses)
  - `KosPair_Cons`
  - `KosStackBlock_Drop`
  - `KosStackBlock_TailPtr`
  - `PyList_Append`
  - `PyList_GET_SIZE`
  - `PyList_New`
  - `pair_splice_extend`

### OP_PARAM
**Source:** [`koskari/lib/opcode/PARAM.c`](koskari/lib/opcode/PARAM.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosCode_GetConstant`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosParamRef_New`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_POP
**Source:** [`koskari/lib/opcode/POP.c`](koskari/lib/opcode/POP.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `kos_op_outcome_from_int_step` (2 uses)
  - `KosStackBlock_Drop`
  - `KosStackBlock_Drop1`

- **Callees**
  - `kos_op_outcome_from_int_step` (2 uses)
  - `KosStackBlock_Drop`
  - `KosStackBlock_Drop1`
  - `likely`

### OP_POP_CC
**Source:** [`koskari/lib/opcode/POP_CC.c`](koskari/lib/opcode/POP_CC.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `KosContext_DYN_TOP` (3 uses)

- **Structured accessors and outcome macros**
  - `KosDynNode_IS_CC` (2 uses)
  - `KOS_OP_POST_ERR`
  - `KOS_OP_RUN`

- **Callees**
  - `KosDynNode_IS_CC`
  - `PyErr_SetString`
  - `kos_dyn_pop_cc`

### OP_POP_COLLECT
**Source:** [`koskari/lib/opcode/POP_COLLECT.c`](koskari/lib/opcode/POP_COLLECT.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS` (3 uses)
  - `KosContext_DYN_TOP` (3 uses)
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosDynNode_IS_COLLECT` (2 uses)
  - `KosDynCollect_SAVED_NS`
  - `KosStackBlock_DropBlock`
  - `KosStackBlock_PushRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `Py_INCREF` (2 uses)
  - `KosDynCollect_SAVED_NS`
  - `KosDynNode_IS_COLLECT`
  - `KosStackBlock_DropBlock`
  - `KosStackBlock_PushRef`
  - `PyErr_SetString`
  - `kos_dyn_pop_collect`
  - `kos_op_outcome_from_int_step`
  - `unlikely`

### OP_POP_DYN_WITH
**Source:** [`koskari/lib/opcode/POP_DYN_WITH.c`](koskari/lib/opcode/POP_DYN_WITH.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (3 uses)
  - `CURRENT_ACTING_PC`
  - `CURRENT_FRAME`
  - `CURRENT_PC`
  - `KosContext_DYN_TOP`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (5 uses)
  - `KOS_OP_RUN` (2 uses)
  - `KosDynFrame_ACTING_PC` (2 uses)
  - `KosDynNode_IS_WITH` (2 uses)
  - `KOS_OP_POST_SUSPEND`
  - `KosStackBlock_DropBlock`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushRef`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `KosDynNode_IS_WITH`
  - `KosStackBlock_DropBlock`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushRef`
  - `PyErr_SetString`
  - `kos_dyn_pop_with`
  - `kos_dyn_with_unwind`

### OP_POP_FINALLY
**Source:** [`koskari/lib/opcode/POP_FINALLY.c`](koskari/lib/opcode/POP_FINALLY.c)

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`
  - `KosContext_DYN_TOP`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosDynFinally_HANDLING_RAISE` (2 uses)
  - `KosDynFinally_HANDLING_RETURN` (2 uses)
  - `KosDynNode_IS_FINALLY` (2 uses)
  - `KOS_OP_PROLOGUE`
  - `KOS_OP_RUN`
  - `KosStackBlock_PopBlock`

- **Callees**
  - `KosDynFinally_HANDLING_RAISE` (2 uses)
  - `KosDynFinally_HANDLING_RETURN` (2 uses)
  - `KosDynNode_IS_FINALLY`
  - `KosStackBlock_PopBlock`
  - `PyErr_GetExcInfo`
  - `PyErr_Occurred`
  - `PyErr_Restore`
  - `PyErr_SetExcInfo`
  - `PyErr_SetString`
  - `kos_dyn_pop_finally`

### OP_POP_FOREACH
**Source:** [`koskari/lib/opcode/POP_FOREACH.c`](koskari/lib/opcode/POP_FOREACH.c)

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)
  - `KosContext_DYN_TOP`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosDynNode_IS_FOREACH` (2 uses)
  - `KOS_OP_RUN`
  - `KosDynForeach_RESULT`
  - `KosStackBlock_DropBlock`
  - `KosStackBlock_PushRef`

- **Callees**
  - `KosDynForeach_RESULT`
  - `KosDynNode_IS_FOREACH`
  - `KosStackBlock_DropBlock`
  - `KosStackBlock_PushRef`
  - `PyErr_SetString`
  - `Py_DECREF`
  - `Py_INCREF`
  - `kos_dyn_pop_foreach`
  - `unlikely`

### OP_POP_LOOP
**Source:** [`koskari/lib/opcode/POP_LOOP.c`](koskari/lib/opcode/POP_LOOP.c)

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)
  - `KosContext_DYN_TOP`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosDynNode_IS_LOOP` (2 uses)
  - `KOS_OP_RUN`
  - `KosDynLoop_RESULT`
  - `KosStackBlock_DropBlock`
  - `KosStackBlock_PushRef`

- **Callees**
  - `KosDynLoop_RESULT`
  - `KosDynNode_IS_LOOP`
  - `KosStackBlock_DropBlock`
  - `KosStackBlock_PushRef`
  - `PyErr_SetString`
  - `Py_DECREF`
  - `Py_INCREF`
  - `kos_dyn_pop_loop`
  - `unlikely`

### OP_POP_NS
**Source:** [`koskari/lib/opcode/POP_NS.c`](koskari/lib/opcode/POP_NS.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `KosContext_DYN_TOP` (3 uses)
  - `CURRENT_NS` (2 uses)

- **Structured accessors and outcome macros**
  - `KosDynNode_IS_NS` (2 uses)
  - `KOS_OP_POST_ERR`
  - `KOS_OP_RUN`
  - `KosDynNs_SAVED_NS`

- **Callees**
  - `KosDynNode_IS_NS`
  - `KosDynNs_SAVED_NS`
  - `PyErr_SetString`
  - `Py_DECREF`
  - `Py_INCREF`
  - `kos_dyn_pop_ns`

### OP_POP_PARAM
**Source:** [`koskari/lib/opcode/POP_PARAM.c`](koskari/lib/opcode/POP_PARAM.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `KosContext_DYN_TOP`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosDynNode_IS_PARAM` (2 uses)
  - `KosDynNode_KIND` (2 uses)
  - `KOS_OP_RUN`

- **Callees**
  - `PyErr_SetString` (2 uses)
  - `KosDynNode_IS_PARAM`
  - `KosDynNode_KIND`
  - `kos_dyn_pop_param`
  - `kos_dynnode_kind_name`
  - `snprintf`

### OP_POP_PROMPT
**Source:** [`koskari/lib/opcode/POP_PROMPT.c`](koskari/lib/opcode/POP_PROMPT.c)

- **Insn / operand**
  - `insn` (3 uses)

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (11 uses)
  - `KosContext_DYN_TOP` (9 uses)
  - `CURRENT_FRAME` (3 uses)
  - `KosContext_DELIMITED_RETURN_FRAME` (3 uses)
  - `KosContext_DELIMITED_RETURN_NODE` (3 uses)
  - `KosContext_INVOCATION_SEGMENT_TOP` (3 uses)
  - `KosContext_DELIMITED_RETURN_PC`
  - `KosContext_FLAGS`
  - `KosContext_PENDING_RETURN_TO_PARENT`
  - `KosContext_PENDING_RETURN_VALUE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (7 uses)
  - `KosDynNode_IS_PROMPT` (4 uses)
  - `KOS_OP_RUN` (3 uses)
  - `KosStackBlock_LEN` (3 uses)
  - `KosDynNode_KIND` (2 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `KOS_CTX_RUNNING`
  - `KOS_OP_POST_SUSPEND`
  - `KosEvent_KIND`
  - `KosEvent_PAYLOAD`
  - `KosStackBlock_PushRef`

- **Callees**
  - `SAYF` (6 uses)
  - `Py_DECREF` (3 uses)
  - `KosDynNode_IS_PROMPT` (2 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `SAY` (2 uses)
  - `KosDynNode_KIND`
  - `KosEvent_KIND`
  - `KosEvent_PAYLOAD`
  - `KosStackBlock_PushRef`
  - `PyErr_SetString`
  - `PyLong_AsLong`
  - `PyLong_Check`
  - `Py_XDECREF`
  - `kos_dyn_finish_delimited_pop_return`
  - `kos_dyn_pop_prompt`
  - `kos_dyn_transition`
  - `kos_dynnode_kind_name`

### OP_POP_SHIFT
**Source:** [`koskari/lib/opcode/POP_SHIFT.c`](koskari/lib/opcode/POP_SHIFT.c)

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (4 uses)
  - `KosContext_PLAN` (3 uses)
  - `KosContext_DYN_TOP` (2 uses)
  - `CURRENT_ACTING_PC`
  - `CURRENT_CODE`
  - `CURRENT_FRAME`
  - `CURRENT_PC`
  - `KosContext_FLAGS`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (8 uses)
  - `KosDynNode_IS_SHIFT` (2 uses)
  - `KOS_CTX_IN_TRANSFER`
  - `KOS_OP_PROLOGUE`
  - `KosStackBlock_LEN`
  - `KosStackBlock_PopRef`
  - `KosTransferPlan_CONTINUATION_REF`
  - `KosTransferPlan_RESUME_FRAME`
  - `KosTransferPlan_RESUME_PC`
  - `KosTransferPlan_RESUME_STACK`

- **Callees**
  - `Py_DECREF` (7 uses)
  - `PyErr_SetString` (5 uses)
  - `SAYF` (2 uses)
  - `kos_extent_desc` (2 uses)
  - `KosDynNode_IS_SHIFT`
  - `KosStackBlock_PopRef`
  - `KosTransferPlan_CONTINUATION_REF`
  - `KosTransferPlan_RESUME_FRAME`
  - `KosTransferPlan_RESUME_PC`
  - `KosTransferPlan_RESUME_STACK`
  - `SAY`
  - `kos_dyn_build_pop_shift_plan`
  - `kos_extent_find_by_pop_pc`
  - `kos_find_nearest_prompt`
  - `kos_free_transfer_plan`

### OP_POP_TRY
**Source:** [`koskari/lib/opcode/POP_TRY.c`](koskari/lib/opcode/POP_TRY.c)

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`
  - `KosContext_DYN_TOP`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosDynNode_IS_TRY` (2 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_PopBlock`

- **Callees**
  - `KosDynNode_IS_TRY`
  - `KosStackBlock_PopBlock`
  - `PyErr_Clear`
  - `PyErr_SetExcInfo`
  - `PyErr_SetString`
  - `kos_dyn_pop_try`

### OP_POP_WIND
**Source:** [`koskari/lib/opcode/POP_WIND.c`](koskari/lib/opcode/POP_WIND.c)

- **Context / frame / stack / code macros**
  - `KosContext_DYN_TOP` (3 uses)
  - `CURRENT_STACK` (2 uses)
  - `KosContext_UNWIND_TARGET_FRAME`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosDynNode_IS_WIND` (2 uses)
  - `KosDynNode_PARENT` (2 uses)
  - `KosDynWind_BODY_RESULT` (2 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosDynWind_BODY_RESULT` (2 uses)
  - `KosDynNode_IS_WIND`
  - `KosDynNode_PARENT`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushRef`
  - `PyErr_SetString`
  - `Py_DECREF`
  - `kos_dyn_pop_top`
  - `kos_op_outcome_from_int_step`

### OP_POP_WITH
**Source:** [`koskari/lib/opcode/POP_WITH.c`](koskari/lib/opcode/POP_WITH.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (3 uses)
  - `CURRENT_ACTING_PC`
  - `CURRENT_FRAME`
  - `CURRENT_PC`
  - `KosContext_DYN_TOP`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (5 uses)
  - `KOS_OP_RUN` (2 uses)
  - `KosDynFrame_ACTING_PC` (2 uses)
  - `KosDynNode_IS_WITH` (2 uses)
  - `KOS_OP_POST_SUSPEND`
  - `KosStackBlock_DropBlock`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushRef`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `SAY` (3 uses)
  - `KosDynNode_IS_WITH`
  - `KosStackBlock_DropBlock`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushRef`
  - `PyErr_SetString`
  - `kos_dyn_pop_with`
  - `kos_dyn_with_unwind`

### OP_POSITION
**Source:** [`koskari/lib/opcode/POSITION.c`](koskari/lib/opcode/POSITION.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `KOS_OP_RUN`

### OP_PROPER
**Source:** [`koskari/lib/opcode/PROPER.c`](koskari/lib/opcode/PROPER.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (3 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_PushRef`
  - `KosStackBlock_TailPtr`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `unlikely` (2 uses)
  - `KosPair_ConsFromPtr`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_PushRef`
  - `KosStackBlock_TailPtr`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_PUSH_CC
**Source:** [`koskari/lib/opcode/PUSH_CC.c`](koskari/lib/opcode/PUSH_CC.c)

- **Context / frame / stack / code macros**
  - `CURRENT_ACTING_PC`
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KOS_OP_RUN`

- **Callees**
  - `PyErr_SetString` (2 uses)
  - `kos_dyn_push_cc`
  - `kos_extent_desc`
  - `kos_extent_find`

### OP_PUSH_COLLECT
**Source:** [`koskari/lib/opcode/PUSH_COLLECT.c`](koskari/lib/opcode/PUSH_COLLECT.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS` (4 uses)
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (6 uses)
  - `KOS_OP_RUN`
  - `KosCode_CPOOL_LEN`
  - `KosCode_GetConstant`
  - `KosNamespace_Check`
  - `KosStackBlock_PushBlock`

- **Callees**
  - `Py_DECREF` (5 uses)
  - `PyErr_Format` (2 uses)
  - `KosCollectorNamespace_New`
  - `KosNamespaceSpec_CheckExact`
  - `KosStackBlock_PushBlock`
  - `Py_INCREF`
  - `kos_dyn_pop_collect`
  - `kos_dyn_push_collect`

### OP_PUSH_DYN_WITH
**Source:** [`koskari/lib/opcode/PUSH_DYN_WITH.c`](koskari/lib/opcode/PUSH_DYN_WITH.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (4 uses)
  - `CURRENT_ACTING_PC` (2 uses)
  - `CURRENT_CODE`
  - `CURRENT_FRAME`
  - `CURRENT_PC`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (7 uses)
  - `KosDynFrame_ACTING_PC` (2 uses)
  - `KosStackBlock_DropBlock` (2 uses)
  - `KOS_OP_POST_SUSPEND`
  - `KOS_OP_RUN`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushBlock`

- **Callees**
  - `Py_DECREF` (15 uses)
  - `KosStackBlock_DropBlock` (2 uses)
  - `PyErr_SetString` (2 uses)
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushBlock`
  - `Py_XINCREF`
  - `get_name_from_insn`
  - `kos_dyn_pop_with`
  - `kos_dyn_push_dyn_with_provisional`
  - `kos_dyn_wind`
  - `kos_extent_desc`
  - `kos_extent_find`
  - `resolve_context_enter_exit`

### OP_PUSH_FINALLY
**Source:** [`koskari/lib/opcode/PUSH_FINALLY.c`](koskari/lib/opcode/PUSH_FINALLY.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_PushBlock`

- **Callees**
  - `KosStackBlock_PushBlock`
  - `PyErr_Clear`
  - `PyErr_NoMemory`
  - `PyErr_SetString`
  - `kos_dyn_abort_push_region`
  - `kos_dyn_push_finally`

### OP_PUSH_FOREACH
**Source:** [`koskari/lib/opcode/PUSH_FOREACH.c`](koskari/lib/opcode/PUSH_FOREACH.c)

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (3 uses)
  - `CURRENT_ACTING_PC`
  - `CURRENT_CODE`
  - `CURRENT_PC`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (7 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `KOS_OP_RUN`
  - `KosDynForeach_EXHAUSTED`
  - `KosDynForeach_ITER`
  - `KosDynForeach_RESULT`
  - `KosStackBlock_PushBlock`

- **Callees**
  - `Py_DECREF` (11 uses)
  - `PyErr_SetString` (5 uses)
  - `Py_TYPE` (3 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `KosDynForeach_EXHAUSTED`
  - `KosDynForeach_ITER`
  - `KosDynForeach_RESULT`
  - `KosPyAsyncGen_Check`
  - `KosStackBlock_PushBlock`
  - `PyErr_Clear`
  - `PyErr_NoMemory`
  - `PyObject_GetIter`
  - `Py_INCREF`
  - `am_aiter`
  - `kos_dyn_abort_push_region`
  - `kos_dyn_push_foreach`
  - `kos_extent_desc`
  - `kos_extent_find`

### OP_PUSH_LOOP
**Source:** [`koskari/lib/opcode/PUSH_LOOP.c`](koskari/lib/opcode/PUSH_LOOP.c)

- **Context / frame / stack / code macros**
  - `CURRENT_ACTING_PC`
  - `CURRENT_CODE`
  - `CURRENT_PC`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KOS_OP_RUN`
  - `KosDynLoop_RESULT`
  - `KosStackBlock_PushBlock`

- **Callees**
  - `PyErr_SetString` (3 uses)
  - `KosDynLoop_RESULT`
  - `KosStackBlock_PushBlock`
  - `PyErr_Clear`
  - `PyErr_NoMemory`
  - `Py_INCREF`
  - `kos_dyn_abort_push_region`
  - `kos_dyn_push_loop`
  - `kos_extent_desc`
  - `kos_extent_find`

### OP_PUSH_NS
**Source:** [`koskari/lib/opcode/PUSH_NS.c`](koskari/lib/opcode/PUSH_NS.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_NS` (3 uses)
  - `CURRENT_STACK` (2 uses)
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (11 uses)
  - `KosCode_CPOOL_LEN` (2 uses)
  - `KosCode_GetConstant` (2 uses)
  - `KOS_OP_RUN`
  - `KosCode_EXTRA_LEN`
  - `KosCode_GetExtra`
  - `KosNamespace_Check`
  - `KosNamespace_FromSpec`
  - `KosNamespace_Store`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`

- **Callees**
  - `unlikely` (12 uses)
  - `Py_DECREF` (9 uses)
  - `PyErr_Format` (5 uses)
  - `Py_INCREF` (2 uses)
  - `KosNamespaceSpec_CheckExact`
  - `KosStackBlock_DropNoErr`
  - `KosStackBlock_TailPtr`
  - `PyErr_SetString`
  - `PyTuple_CheckExact`
  - `PyTuple_GET_ITEM`
  - `PyTuple_GET_SIZE`
  - `kos_dyn_push_ns`

### OP_PUSH_PARAM
**Source:** [`koskari/lib/opcode/PUSH_PARAM.c`](koskari/lib/opcode/PUSH_PARAM.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KOS_OP_RUN`
  - `KosCode_GetConstant`
  - `KosStackBlock_PopTailDictFromNames`

- **Callees**
  - `KosStackBlock_PopTailDictFromNames`
  - `Py_DECREF`
  - `kos_dyn_push_param_dict`

### OP_PUSH_PROMPT
**Source:** [`koskari/lib/opcode/PUSH_PROMPT.c`](koskari/lib/opcode/PUSH_PROMPT.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KOS_OP_RUN`

- **Callees**
  - `get_prompt_id_from_insn`
  - `kos_dyn_push_prompt`

### OP_PUSH_SHIFT
**Source:** [`koskari/lib/opcode/PUSH_SHIFT.c`](koskari/lib/opcode/PUSH_SHIFT.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_ACTING_PC` (4 uses)
  - `CURRENT_CODE` (3 uses)
  - `CURRENT_PC` (3 uses)
  - `KosContext_PLAN` (3 uses)
  - `KosContext_DYN_TOP` (2 uses)
  - `CURRENT_FRAME`
  - `KosContext_FLAGS`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (11 uses)
  - `KosDynPrompt_PENDING_SHIFT_WRAPPER` (4 uses)
  - `KosDynFrame_ACTING_PC` (2 uses)
  - `kos_op_outcome_from_int_step` (2 uses)
  - `KOS_CTX_IN_TRANSFER`
  - `KOS_OP_PROLOGUE`
  - `KosTransferPlan_CONTINUATION_REF`

- **Callees**
  - `KosDynPrompt_PENDING_SHIFT_WRAPPER` (4 uses)
  - `PyErr_SetString` (3 uses)
  - `KosPyDelimitedContinuation_New` (2 uses)
  - `Py_DECREF` (2 uses)
  - `kos_continuation_free` (2 uses)
  - `kos_dyn_push_shift` (2 uses)
  - `kos_op_outcome_from_int_step` (2 uses)
  - `kos_shift_capture` (2 uses)
  - `kos_shift_ic_for` (2 uses)
  - `push_result` (2 uses)
  - `KosTransferPlan_CONTINUATION_REF`
  - `SAY`
  - `SAYF`
  - `get_prompt_id_from_insn`
  - `kos_dyn_build_push_shift_unwind_plan`
  - `kos_find_prompt`
  - `kos_free_transfer_plan`

### OP_PUSH_TRY
**Source:** [`koskari/lib/opcode/PUSH_TRY.c`](koskari/lib/opcode/PUSH_TRY.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_PushBlock`

- **Callees**
  - `KosStackBlock_PushBlock`
  - `PyErr_Clear`
  - `PyErr_NoMemory`
  - `PyErr_SetString`
  - `kos_dyn_abort_push_region`
  - `kos_dyn_push_try`

### OP_PUSH_WIND
**Source:** [`koskari/lib/opcode/PUSH_WIND.c`](koskari/lib/opcode/PUSH_WIND.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (7 uses)
  - `KOS_OP_RUN` (2 uses)
  - `KosCallable_Check` (2 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `KosDynWind_ENTERED`

- **Callees**
  - `Py_DECREF` (6 uses)
  - `Py_XDECREF` (5 uses)
  - `PyErr_SetString` (4 uses)
  - `KosCallable_Check` (2 uses)
  - `KosStackBlock_PopRef` (2 uses)
  - `KosDynWind_ENTERED`
  - `PyErr_Format`
  - `kos_dyn_push_wind`
  - `kos_dyn_wind`

### OP_PUSH_WITH
**Source:** [`koskari/lib/opcode/PUSH_WITH.c`](koskari/lib/opcode/PUSH_WITH.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (4 uses)
  - `CURRENT_ACTING_PC` (2 uses)
  - `CURRENT_CODE`
  - `CURRENT_FRAME`
  - `CURRENT_PC`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (7 uses)
  - `KosDynFrame_ACTING_PC` (2 uses)
  - `KosStackBlock_DropBlock` (2 uses)
  - `KOS_OP_POST_SUSPEND`
  - `KOS_OP_RUN`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushBlock`

- **Callees**
  - `Py_DECREF` (15 uses)
  - `KosStackBlock_DropBlock` (2 uses)
  - `PyErr_SetString` (2 uses)
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushBlock`
  - `Py_XINCREF`
  - `get_name_from_insn`
  - `kos_dyn_pop_with`
  - `kos_dyn_push_with_provisional`
  - `kos_dyn_wind`
  - `kos_extent_desc`
  - `kos_extent_find`
  - `resolve_context_enter_exit`

### OP_RAISE
**Source:** [`koskari/lib/opcode/RAISE.c`](koskari/lib/opcode/RAISE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (5 uses)
  - `KOS_OP_PROLOGUE`
  - `KosStackBlock_PopRef`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `Py_XDECREF` (3 uses)
  - `PyErr_SetObject` (2 uses)
  - `PyErr_SetString` (2 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_Format`
  - `PyErr_GetExcInfo`
  - `PyErr_Occurred`
  - `PyErr_Restore`
  - `PyErr_SetExcInfo`
  - `PyExceptionClass_Check`
  - `PyExceptionInstance_Check`
  - `PyObject_CallNoArgs`
  - `Py_TYPE`

### OP_RETURN
**Source:** [`koskari/lib/opcode/RETURN.c`](koskari/lib/opcode/RETURN.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (8 uses)
  - `CURRENT_FRAME` (4 uses)
  - `KosContext_FLAGS` (2 uses)
  - `KosContext_PENDING_RETURN_TO_PARENT` (2 uses)
  - `KosContext_PENDING_RETURN_VALUE` (2 uses)
  - `CURRENT_PC`
  - `KosContext_PLAN`
  - `KosContext_RETURN_TARGET_CHAIN`
  - `KosContext_RETURN_TARGET_PC`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (8 uses)
  - `KOS_OP_RUN` (5 uses)
  - `KosDynNode_IS_GEN` (2 uses)
  - `KosDynNode_PARENT` (2 uses)
  - `KosEvent_KIND` (2 uses)
  - `KosEvent_PAYLOAD` (2 uses)
  - `KosStackBlock_LEN` (2 uses)
  - `KOS_CTX_DEAD`
  - `KOS_CTX_RUNNING`
  - `KOS_OP_POST_PARENT_RETURN`
  - `KOS_OP_POST_SUSPEND`
  - `KOS_OP_PROLOGUE`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PopTail`

- **Callees**
  - `Py_DECREF` (5 uses)
  - `SAYF` (4 uses)
  - `KosEvent_KIND` (2 uses)
  - `KosEvent_PAYLOAD` (2 uses)
  - `PyErr_SetString` (2 uses)
  - `kos_dyn_transition` (2 uses)
  - `unlikely` (2 uses)
  - `KosDynNode_IS_GEN`
  - `KosDynNode_PARENT`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PopTail`
  - `Py_INCREF`
  - `SAY`
  - `kos_dyn_pop_frame`
  - `kos_return_delimited`

### OP_TAIL_CALL
**Source:** [`koskari/lib/opcode/TAIL_CALL.c`](koskari/lib/opcode/TAIL_CALL.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_FRAME`

- **Structured accessors and outcome macros**
  - `KosDynFrame_STACK` (2 uses)
  - `KOS_OP_POST_ERR`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`

- **Callees**
  - `KosContext_Call`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`

### OP_TAIL_CALL_NAMES
**Source:** [`koskari/lib/opcode/TAIL_CALL_NAMES.c`](koskari/lib/opcode/TAIL_CALL_NAMES.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosStackBlock_Peek`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `kos_op_outcome_from_call_int`

- **Callees**
  - `KosContext_Call`
  - `KosStackBlock_Peek`
  - `KosStackSteal_Free`
  - `KosStackSteal_Tail`
  - `PyErr_Format`
  - `PyTuple_CheckExact`
  - `PyTuple_GET_SIZE`
  - `kos_op_outcome_from_call_int`

### OP_TAIL_CALL_STAR
**Source:** [`koskari/lib/opcode/TAIL_CALL_STAR.c`](koskari/lib/opcode/TAIL_CALL_STAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (12 uses)
  - `KosStackBlock_PopRef` (8 uses)
  - `kos_op_outcome_from_call_int`

- **Callees**
  - `Py_DECREF` (15 uses)
  - `KosStackBlock_PopRef` (8 uses)
  - `PyTuple_New` (2 uses)
  - `ensure_tuple_star` (2 uses)
  - `KosContext_CallArgs`
  - `Py_XDECREF`
  - `kos_op_outcome_from_call_int`

### OP_THEEND
**Source:** [`koskari/lib/opcode/THEEND.c`](koskari/lib/opcode/THEEND.c)

- **Insn / operand**
  - `insn`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR`

- **Callees**
  - `PyErr_SetString`

### OP_UNPACK
**Source:** [`koskari/lib/opcode/UNPACK.c`](koskari/lib/opcode/UNPACK.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushRef`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PushRef`
  - `PyErr_Format`
  - `PySequence_Fast`
  - `PySequence_Fast_GET_ITEM`
  - `PySequence_Fast_GET_SIZE`

### OP_UNPACK_FAST
**Source:** [`koskari/lib/opcode/UNPACK_FAST.c`](koskari/lib/opcode/UNPACK_FAST.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_NS`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (7 uses)
  - `KosNamespace_FAST_COUNT` (2 uses)
  - `KosNamespace_FastStore` (2 uses)
  - `KOS_OP_RUN`
  - `KosCode_GetExtra`
  - `KosNamespace_SupportsFast`
  - `KosStackBlock_PopRef`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `PyErr_Format` (3 uses)
  - `PySequence_Fast_GET_ITEM` (2 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_SetString`
  - `PySequence_Fast`
  - `PySequence_Fast_GET_SIZE`

### OP_UNPACK_STAR
**Source:** [`koskari/lib/opcode/UNPACK_STAR.c`](koskari/lib/opcode/UNPACK_STAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (4 uses)
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (9 uses)
  - `KosStackBlock_PushRef` (2 uses)
  - `KOS_OP_RUN`
  - `KosCode_GetExtra`
  - `KosStackBlock_PUSH`
  - `KosStackBlock_PopRef`

- **Callees**
  - `Py_DECREF` (9 uses)
  - `KosStackBlock_PushRef` (2 uses)
  - `PySequence_Fast_GET_ITEM` (2 uses)
  - `KosStackBlock_PUSH`
  - `KosStackBlock_PopRef`
  - `PyErr_Format`
  - `PyErr_SetString`
  - `PySequence_Fast`
  - `PySequence_Fast_GET_SIZE`
  - `PySequence_GetSlice`
  - `PySequence_Tuple`

### OP_UNPACK_VAR
**Source:** [`koskari/lib/opcode/UNPACK_VAR.c`](koskari/lib/opcode/UNPACK_VAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (6 uses)
  - `KosCode_GetConstant` (2 uses)
  - `KOS_OP_RUN`
  - `KosCode_GetExtra`
  - `KosStackBlock_PopRef`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `PySequence_Fast_GET_ITEM` (2 uses)
  - `frame_ns_store` (2 uses)
  - `KosStackBlock_PopRef`
  - `PyErr_Format`
  - `PySequence_Fast`
  - `PySequence_Fast_GET_SIZE`

### OP_VAR
**Source:** [`koskari/lib/opcode/VAR.c`](koskari/lib/opcode/VAR.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `frame_ns_deref`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_VAR_ATTR
**Source:** [`koskari/lib/opcode/VAR_ATTR.c`](koskari/lib/opcode/VAR_ATTR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosCode_GetConstant` (2 uses)
  - `KosCode_GetExtra`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `PyObject_GetAttr`
  - `Py_DECREF`
  - `frame_ns_deref`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_VAR_ATTR_CLEAR
**Source:** [`koskari/lib/opcode/VAR_ATTR_CLEAR.c`](koskari/lib/opcode/VAR_ATTR_CLEAR.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosCode_GetConstant` (2 uses)
  - `KOS_OP_RUN`
  - `KosCode_GetExtra`

- **Callees**
  - `PyObject_DelAttr`
  - `Py_DECREF`
  - `frame_ns_deref`

### OP_VAR_ATTR_REF
**Source:** [`koskari/lib/opcode/VAR_ATTR_REF.c`](koskari/lib/opcode/VAR_ATTR_REF.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KosCode_GetConstant` (2 uses)
  - `KosCode_GetExtra`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosAttributeRef_New`
  - `Py_DECREF`
  - `frame_ns_deref`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_VAR_ATTR_STORE
**Source:** [`koskari/lib/opcode/VAR_ATTR_STORE.c`](koskari/lib/opcode/VAR_ATTR_STORE.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosCode_GetConstant` (2 uses)
  - `KOS_OP_RUN`
  - `KosCode_GetExtra`
  - `KosStackBlock_PopRef`

- **Callees**
  - `Py_DECREF` (3 uses)
  - `KosStackBlock_PopRef`
  - `PyObject_SetAttr`
  - `frame_ns_deref`

### OP_VAR_BIND
**Source:** [`koskari/lib/opcode/VAR_BIND.c`](koskari/lib/opcode/VAR_BIND.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `KosStackBlock_PopRef`
  - `Py_DECREF`
  - `frame_ns_bind`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`

### OP_VAR_CLEAR
**Source:** [`koskari/lib/opcode/VAR_CLEAR.c`](koskari/lib/opcode/VAR_CLEAR.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KOS_OP_RUN`

- **Callees**
  - `frame_ns_clear`
  - `get_name_from_insn`

### OP_VAR_OBJECT_ADD
**Source:** [`koskari/lib/opcode/VAR_OBJECT_ADD.c`](koskari/lib/opcode/VAR_OBJECT_ADD.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyNumber_InPlaceAdd`
  - `frame_ns_deref`
  - `frame_ns_store`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`

### OP_VAR_OBJECT_BIT_AND
**Source:** [`koskari/lib/opcode/VAR_OBJECT_BIT_AND.c`](koskari/lib/opcode/VAR_OBJECT_BIT_AND.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyNumber_InPlaceAnd`
  - `frame_ns_deref`
  - `frame_ns_store`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`

### OP_VAR_OBJECT_BIT_LEFT
**Source:** [`koskari/lib/opcode/VAR_OBJECT_BIT_LEFT.c`](koskari/lib/opcode/VAR_OBJECT_BIT_LEFT.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyNumber_InPlaceLshift`
  - `frame_ns_deref`
  - `frame_ns_store`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`

### OP_VAR_OBJECT_BIT_OR
**Source:** [`koskari/lib/opcode/VAR_OBJECT_BIT_OR.c`](koskari/lib/opcode/VAR_OBJECT_BIT_OR.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyNumber_InPlaceOr`
  - `frame_ns_deref`
  - `frame_ns_store`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`

### OP_VAR_OBJECT_BIT_RIGHT
**Source:** [`koskari/lib/opcode/VAR_OBJECT_BIT_RIGHT.c`](koskari/lib/opcode/VAR_OBJECT_BIT_RIGHT.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyNumber_InPlaceRshift`
  - `frame_ns_deref`
  - `frame_ns_store`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`

### OP_VAR_OBJECT_BIT_XOR
**Source:** [`koskari/lib/opcode/VAR_OBJECT_BIT_XOR.c`](koskari/lib/opcode/VAR_OBJECT_BIT_XOR.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyNumber_InPlaceXor`
  - `frame_ns_deref`
  - `frame_ns_store`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`

### OP_VAR_OBJECT_FLOOR_DIVIDE
**Source:** [`koskari/lib/opcode/VAR_OBJECT_FLOOR_DIVIDE.c`](koskari/lib/opcode/VAR_OBJECT_FLOOR_DIVIDE.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyNumber_InPlaceFloorDivide`
  - `frame_ns_deref`
  - `frame_ns_store`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`

### OP_VAR_OBJECT_MATRIX_MULTIPLY
**Source:** [`koskari/lib/opcode/VAR_OBJECT_MATRIX_MULTIPLY.c`](koskari/lib/opcode/VAR_OBJECT_MATRIX_MULTIPLY.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyNumber_InPlaceMatrixMultiply`
  - `frame_ns_deref`
  - `frame_ns_store`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`

### OP_VAR_OBJECT_MODULO
**Source:** [`koskari/lib/opcode/VAR_OBJECT_MODULO.c`](koskari/lib/opcode/VAR_OBJECT_MODULO.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyNumber_InPlaceRemainder`
  - `frame_ns_deref`
  - `frame_ns_store`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`

### OP_VAR_OBJECT_MULTIPLY
**Source:** [`koskari/lib/opcode/VAR_OBJECT_MULTIPLY.c`](koskari/lib/opcode/VAR_OBJECT_MULTIPLY.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyNumber_InPlaceMultiply`
  - `frame_ns_deref`
  - `frame_ns_store`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`

### OP_VAR_OBJECT_POWER
**Source:** [`koskari/lib/opcode/VAR_OBJECT_POWER.c`](koskari/lib/opcode/VAR_OBJECT_POWER.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyNumber_InPlacePower`
  - `frame_ns_deref`
  - `frame_ns_store`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`

### OP_VAR_OBJECT_SUBTRACT
**Source:** [`koskari/lib/opcode/VAR_OBJECT_SUBTRACT.c`](koskari/lib/opcode/VAR_OBJECT_SUBTRACT.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyNumber_InPlaceSubtract`
  - `frame_ns_deref`
  - `frame_ns_store`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`

### OP_VAR_OBJECT_TRUE_DIVIDE
**Source:** [`koskari/lib/opcode/VAR_OBJECT_TRUE_DIVIDE.c`](koskari/lib/opcode/VAR_OBJECT_TRUE_DIVIDE.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (4 uses)
  - `KosStackBlock_PopRef`
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_DECREF` (4 uses)
  - `unlikely` (4 uses)
  - `KosStackBlock_PopRef`
  - `PyNumber_InPlaceTrueDivide`
  - `frame_ns_deref`
  - `frame_ns_store`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`

### OP_VAR_REF
**Source:** [`koskari/lib/opcode/VAR_REF.c`](koskari/lib/opcode/VAR_REF.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `kos_op_outcome_from_int_step`

- **Callees**
  - `Py_INCREF`
  - `frame_ns_ref`
  - `get_name_from_insn`
  - `kos_op_outcome_from_int_step`
  - `push_result`

### OP_VAR_STORE
**Source:** [`koskari/lib/opcode/VAR_STORE.c`](koskari/lib/opcode/VAR_STORE.c)

- **Insn / operand**
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_CODE`
  - `CURRENT_STACK`

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (3 uses)
  - `KOS_OP_RUN`
  - `KosStackBlock_PopRef`

- **Callees**
  - `Py_DECREF` (2 uses)
  - `KosStackBlock_PopRef`
  - `frame_ns_store`
  - `get_name_from_insn`

### OP_YIELD
**Source:** [`koskari/lib/opcode/YIELD.c`](koskari/lib/opcode/YIELD.c)

- **Insn / operand**
  - `KosOp_ARG`
  - `insn`

- **Context / frame / stack / code macros**
  - `CURRENT_STACK` (2 uses)
  - `KosContext_FLAGS` (2 uses)

- **Structured accessors and outcome macros**
  - `KOS_OP_POST_ERR` (2 uses)
  - `KOS_CTX_DEAD`
  - `KOS_CTX_RUNNING`
  - `KOS_OP_POST_SUSPEND`
  - `KosEvent_KIND`
  - `KosEvent_PAYLOAD`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PopTail`

- **Callees**
  - `SAY` (2 uses)
  - `KosEvent_KIND`
  - `KosEvent_PAYLOAD`
  - `KosStackBlock_PopRef`
  - `KosStackBlock_PopTail`
  - `Py_INCREF`

### OP_YIELD_FROM
**Source:** [`koskari/lib/opcode/YIELD_FROM.c`](koskari/lib/opcode/YIELD_FROM.c)

- **Insn / operand**
  - `insn` (4 uses)

- **Context / frame / stack / code macros**
  - `CURRENT_FRAME` (3 uses)
  - `KosContext_DYN_TOP` (2 uses)
  - `KosContext_INJECT_MODE` (2 uses)
  - `KosContext_INJECT_TYPE` (2 uses)
  - `KosContext_INJECT_VALUE` (2 uses)

- **Structured accessors and outcome macros**
  - `KosDynFrame_STACK` (10 uses)
  - `KosDynYieldFrom_PENDING_ARG` (8 uses)
  - `KOS_OP_POST_ERR` (6 uses)
  - `KosDynYieldFrom_PENDING_ACTION` (5 uses)
  - `KosDynYieldFrom_PHASE_SET` (4 uses)
  - `KosStackBlock_PopRef` (4 uses)
  - `kos_op_outcome_from_int_step` (4 uses)
  - `KosDynFrame_ACTING_PC` (2 uses)
  - `KosDynFrame_PC` (2 uses)
  - `KosDynNode_IS_YIELD_FROM` (2 uses)
  - `KosDynYieldFrom_PENDING_ARG_TYPE` (2 uses)
  - `KosDynYieldFrom_PHASE` (2 uses)
  - `KOS_OP_RUN`
  - `KosDynYieldFrom_DELEGATE`
  - `KosDynYieldFrom_KIND`
  - `KosStackBlock_LEN`

- **Callees**
  - `KosDynYieldFrom_PENDING_ARG` (8 uses)
  - `Py_XDECREF` (7 uses)
  - `KosDynYieldFrom_PENDING_ACTION` (5 uses)
  - `KosDynYieldFrom_PHASE_SET` (4 uses)
  - `KosStackBlock_PopRef` (4 uses)
  - `Py_DECREF` (4 uses)
  - `Py_INCREF` (4 uses)
  - `kos_op_outcome_from_int_step` (4 uses)
  - `KosDynYieldFrom_PENDING_ARG_TYPE` (2 uses)
  - `KosDynYieldFrom_PHASE` (2 uses)
  - `KosDynNode_IS_YIELD_FROM`
  - `KosDynYieldFrom_DELEGATE`
  - `KosDynYieldFrom_KIND`
  - `PyErr_SetString`
  - `kos_dyn_push_yield_from`
  - `yield_from_drive_kos_async_gen`
  - `yield_from_drive_py_async`
  - `yield_from_drive_sync_gen`
  - `yield_from_drive_sync_iter`
  - `yield_from_normalize`

---
## Part 2 — Proposed VM access API surface *(conceptual; implemented as `KosEvalState_*`)*

### Design constraints

Opcode handlers are the only scope for Part 1 (when it was current). The API below consolidates access patterns into **named entry points**. In the **current tree**, those entry points live on **`KosEvalState`** (borrowed `ctx` + cached frame/code), not as `KosContext_*` functions. Part 2 keeps the original **`KosContext_*` labels** as **group titles** for historical continuity; see the **Implementation mapping** table for real symbols.

### Implementation mapping (complete vs remaining gaps)

| Part 2 group (original name) | Implemented as (current) | Notes |
|------------------------------|---------------------------|--------|
| `KosContext_GetConstant` | **`KosEvalState_GetConstant`** | |
| `KosContext_GetVar` | **`KosEvalState_GetVar`** | |
| `KosContext_GetFast` | **`KosEvalState_GetFast`** | |
| `KosContext_SetFast` | **`KosEvalState_SetFast`** | |
| `KosContext_StackPushRef` / pop / tail / dict / steal / peek / len / drop / duplicate / clear block | **`KosEvalState_Stack*`** family in `eval.h` (`StackPushRef`, `StackPopRef`, `StackPopTail`, `StackSteal_Tail`, `StackPUSH`, `ParentStack*`, …) | All use **`KosContext_BLOCK(ctx)`** internally. |
| `KosContext_StackPushBlock` / `StackDropBlock` | **No `KosEvalState_*` wrapper** | **`KosStackBlock_PushBlock` / `DropBlock`** still exist in `stack.c`, but opcode paths typically allocate stack segments via **`kos_dyn_push_*`** / region setup, not a standalone context helper. Treat Part 2 wording as **conceptual**. |
| `KosContext_GetFramePc` (read/write PC) | **`KosEvalState_PC`**, **`KosEvalState_SET_PC`**, **`KosEvalState_ACTING_PC`** (macros; `acting_pc` set only by instruction fetch; live regs on `KosEvalState` until freeze) | |
| `KosContext_CurrentNamespace` | **`KosEvalState_CurrentNamespace`** | Also **`KosEvalState_CurrentCode`**. |
| `KosContext_CallableCall` | **`KosEvalState_Call`**, **`KosEvalState_CallArgs`**, **`KosEvalState_CallNoArgs`** | Underlying Koskari callables use **`KosCallable_Call`** / **`KosCallable_CallArgs`** (`callable.c`) with **`KosEvalState *`** (install slots and native handlers take **`es`** first; use **`KosEvalState_CONTEXT(es)`** for heap context). |
| `KosContext_DynTop` | **`KosContext_DYN_TOP`** macro (+ **`kos_dyn_*`**) | Not a function; **`KosEvalState_RefreshDynCaches`** / **`KosEvalState_InnermostStackOwner`** for cache discipline. |
| `KosContext_TransferPlan` | **`KosContext_PLAN`**, **`KosContext_PLAN_STACK`**, **`KosContext_FLAGS`** macros | |
| `KosContext_PendingReturn` | **`KosContext_PENDING_RETURN_*`** macros | |
| `KosContext_InjectedException` | **`KosContext_INJECT_*`** macros | |
| `KosContext_BoundaryFlags` | **`KosContext_FLAGS`**, **`KosContext_SUSPENSION_EVENT`** | |
| `KosContext_OpOutcome` | **`KosOpResult`**, **`EVAL_NORMAL`**, **`EVAL_RAISE`**, **`EVAL_YIELD`**, **`EVAL_AWAIT`**, **`EVAL_RETURN`**, **`EVAL_PARENT_RETURN`**, **`EVAL_CONTROL_RETURNED`**, **`EVAL_FATAL`** | `KosOpOutcome` / `KOS_OP_*` macros deleted (Phase 6); opcode handlers now return `KosOpResult` via `EVAL_*` macros in `eval.h`. |
| `KosContext_GlobalsFrame` | **`KosContext_GLOBALS_FRAME`**, **`KosContext_GLOBALS_CACHE_VERSION`** | |

**Call discipline:** Mutating the dynamic chain from opcode handlers and drivers should go through APIs that take **`KosEvalState *`** (`kos_dyn_*`, `kos_dyn_transition`, `KosEvalState_SyncFromContext` after chain changes). See comments on **`KosEvalState`** in `eval.h`.

### Grouped API (names and responsibilities)

*Original proposal used `KosContext_*` prefixes; implementation uses the mapping table above.*

#### `KosContext_GetConstant`

- **Group:** Constants / pool
- **Responsibility:** Load a constant pool entry for the current code object using an index or insn operand.
- **Wraps / replaces (conceptual):** `KosCode_CPOOL`, `KosCode_GET_CONST`, `KosCode_CPOOL_LEN`, `CONST` opcode family
- **Motivating opcodes (Part 1 heuristic):** `BUILD_KWDS_NAMES`, `CALL_ATTR`, `CALL_ATTR_NAMES`, `CALL_ATTR_STAR`, `CALL_FAST`, `CALL_FAST_ATTR`, `CALL_FAST_ATTR_NAMES`, `CALL_FAST_ATTR_STAR`, `CALL_FAST_NAMES`, `CALL_FAST_STAR`, `CALL_VAR`, `CALL_VAR_ATTR`, `CALL_VAR_ATTR_NAMES`, `CALL_VAR_ATTR_STAR`, `CALL_VAR_NAMES`, `CALL_VAR_STAR`, `CONST`, `CONST_ITEM`, `CONST_ITEM_CLEAR`, `CONST_ITEM_REF`, `CONST_ITEM_STORE`, `FAST_ATTR`, `FAST_ATTR_CLEAR`, `FAST_ATTR_REF`, `FAST_ATTR_STORE`, `OBJECT_FORMAT_CONST`, `PARAM`, `PUSH_COLLECT`, `PUSH_NS`, `PUSH_PARAM`, `UNPACK_FAST`, `UNPACK_STAR`, `UNPACK_VAR`, `VAR_ATTR`, `VAR_ATTR_CLEAR`, `VAR_ATTR_REF`, `VAR_ATTR_STORE`

#### `KosContext_GetVar`

- **Group:** Variables (dynamic and fast)
- **Responsibility:** Resolve and optionally push a dynamic variable by name from the current namespace.
- **Wraps / replaces (conceptual):** `VAR`, `VAR_REF`, `VAR_ATTR`, `get_name_from_insn`, `KosNamespace_*` deref patterns
- **Motivating opcodes (Part 1 heuristic):** `ATTR`, `ATTR_CLEAR`, `ATTR_REF`, `ATTR_STORE`, `IMPORT`, `IMPORT_FROM`, `IMPORT_REL`, `PUSH_DYN_WITH`, `PUSH_WITH`, `VAR`, `VAR_ATTR`, `VAR_ATTR_CLEAR`, `VAR_ATTR_REF`, `VAR_ATTR_STORE`, `VAR_BIND`, `VAR_CLEAR`, `VAR_OBJECT_ADD`, `VAR_OBJECT_BIT_AND`, `VAR_OBJECT_BIT_LEFT`, `VAR_OBJECT_BIT_OR`, `VAR_OBJECT_BIT_RIGHT`, `VAR_OBJECT_BIT_XOR`, `VAR_OBJECT_FLOOR_DIVIDE`, `VAR_OBJECT_MATRIX_MULTIPLY`, `VAR_OBJECT_MODULO`, `VAR_OBJECT_MULTIPLY`, `VAR_OBJECT_POWER`, `VAR_OBJECT_SUBTRACT`, `VAR_OBJECT_TRUE_DIVIDE`, `VAR_REF`, `VAR_STORE`

#### `KosContext_GetFast`

- **Group:** Variables (dynamic and fast)
- **Responsibility:** Read a fast local slot on the current namespace.
- **Wraps / replaces (conceptual):** `FAST`, `FAST_REF`, `KosNamespace_FastDeref`
- **Motivating opcodes (Part 1 heuristic):** `CALL_FAST`, `CALL_FAST_ATTR`, `CALL_FAST_ATTR_NAMES`, `CALL_FAST_ATTR_STAR`, `CALL_FAST_NAMES`, `CALL_FAST_STAR`, `FAST`, `FAST_ATTR`, `FAST_ATTR_CLEAR`, `FAST_ATTR_REF`, `FAST_ATTR_STORE`, `FAST_BIND`, `FAST_CLEAR`, `FAST_OBJECT_ADD`, `FAST_OBJECT_BIT_AND`, `FAST_OBJECT_BIT_LEFT`, `FAST_OBJECT_BIT_OR`, `FAST_OBJECT_BIT_RIGHT`, `FAST_OBJECT_BIT_XOR`, `FAST_OBJECT_FLOOR_DIVIDE`, `FAST_OBJECT_MATRIX_MULTIPLY`, `FAST_OBJECT_MODULO`, `FAST_OBJECT_MULTIPLY`, `FAST_OBJECT_POWER`, `FAST_OBJECT_SUBTRACT`, `FAST_OBJECT_TRUE_DIVIDE`, `FAST_REF`, `FAST_STORE`

#### `KosContext_SetFast`

- **Group:** Variables (dynamic and fast)
- **Responsibility:** Store into a fast local slot.
- **Wraps / replaces (conceptual):** `FAST_STORE`, `FAST_BIND`, `FAST_CLEAR`, `FAST_ATTR_STORE`
- **Motivating opcodes (Part 1 heuristic):** `FAST_ATTR_CLEAR`, `FAST_ATTR_STORE`, `FAST_BIND`, `FAST_CLEAR`, `FAST_STORE`

#### `KosContext_StackPushRef`

- **Group:** Stack
- **Responsibility:** Push a value onto the operand stack for the current frame (ref semantics).
- **Wraps / replaces (conceptual):** `KosStackBlock_PushRef`, `push_result`, `KosStack_Extend` family
- **Motivating opcodes (Part 1 heuristic):** `ATTR`, `ATTR_REF`, `BUILD_ARGS`, `BUILD_CLASS`, `BUILD_FUNCTION`, `BUILD_KWDS`, `BUILD_KWDS_NAMES`, `CAPTURE_CC`, `CAR`, `CDR`, `COMBINE_ARGS`, `COMBINE_KWDS`, `CONS`, `CONST`, `CONST_ITEM`, `CONST_ITEM_REF`, `DEREF`, `EXCEPTION`, `EXCEPTION_MATCH`, `FAST`, `FAST_ATTR`, `FAST_ATTR_REF`, `FAST_REF`, `GLOBALS`, `IMPORT`, `IMPORT_FROM`, `IMPORT_REL`, `ITEM`, `ITEM_REF`, `ITER_NEXT`, `JUMP_AND`, `JUMP_OR`, `LOCALS`, `NEXT`, `OBJECT_AITER`, `OBJECT_BIT_INVERT`, `OBJECT_DIR`, `OBJECT_DIVMOD`, `OBJECT_EQ`, `OBJECT_FORMAT`, `OBJECT_FORMAT_CONST`, `OBJECT_GE`, `OBJECT_GT`, `OBJECT_IS_INSTANCE`, `OBJECT_IS_SUBCLASS`, `OBJECT_ITER`, `OBJECT_LE`, `OBJECT_LEN`, `OBJECT_LT`, `OBJECT_MODULO`, `OBJECT_NE`, `OBJECT_NOT`, `OBJECT_POWER`, `OBJECT_REPR`, `OBJECT_TRUTH`, `OBJECT_TYPE`, `OBJECT_VARS`, `PAIR_SPLICE`, `PARAM`, `POP_COLLECT`, `POP_DYN_WITH`, `POP_FOREACH`, `POP_LOOP`, `POP_PROMPT`, `POP_WIND`, `POP_WITH`, `PROPER`, `PUSH_SHIFT`, `UNPACK`, `UNPACK_STAR`, `VAR`, `VAR_ATTR`, `VAR_ATTR_REF`, `VAR_REF`

#### `KosContext_StackPopRef`

- **Group:** Stack
- **Responsibility:** Pop one or more operands; return borrowed or owned refs per existing stack API.
- **Wraps / replaces (conceptual):** `KosStackBlock_PopRef`, `PopTail`, `Drop`, `TailPtr`, `Peek`
- **Motivating opcodes (Part 1 heuristic):** `ATTR`, `ATTR_CLEAR`, `ATTR_REF`, `ATTR_STORE`, `AWAIT`, `BREAK`, `BUILD_ARGS`, `BUILD_CLASS`, `BUILD_FUNCTION`, `BUILD_KWDS`, `BUILD_KWDS_NAMES`, `CALL`, `CALL_ATTR`, `CALL_ATTR_NAMES`, `CALL_ATTR_STAR`, `CALL_FAST`, `CALL_FAST_ATTR`, `CALL_FAST_ATTR_NAMES`, `CALL_FAST_ATTR_STAR`, `CALL_FAST_NAMES`, `CALL_FAST_STAR`, `CALL_NAMES`, `CALL_STAR`, `CALL_VAR`, `CALL_VAR_ATTR`, `CALL_VAR_ATTR_NAMES`, `CALL_VAR_ATTR_STAR`, `CALL_VAR_NAMES`, `CALL_VAR_STAR`, `CAR`, `CDR`, `CHECK_EXCEPTION`, `COMBINE_ARGS`, `COMBINE_KWDS`, `CONS`, `CONST_ITEM`, `CONST_ITEM_CLEAR`, `CONST_ITEM_REF`, `CONST_ITEM_STORE`, `CONTINUE`, `DEREF`, `EXCEPTION_MATCH`, `FAST_ATTR_STORE`, `FAST_BIND`, `FAST_OBJECT_ADD`, `FAST_OBJECT_BIT_AND`, `FAST_OBJECT_BIT_LEFT`, `FAST_OBJECT_BIT_OR`, `FAST_OBJECT_BIT_RIGHT`, `FAST_OBJECT_BIT_XOR`, `FAST_OBJECT_FLOOR_DIVIDE`, `FAST_OBJECT_MATRIX_MULTIPLY`, `FAST_OBJECT_MODULO`, `FAST_OBJECT_MULTIPLY`, `FAST_OBJECT_POWER`, `FAST_OBJECT_SUBTRACT`, `FAST_OBJECT_TRUE_DIVIDE`, `FAST_STORE`, `IMPORT_FROM`, `ITEM`, `ITEM_CLEAR`, `ITEM_REF`, `JUMP_AND`, `JUMP_FALSE`, `JUMP_OR`, `JUMP_TRUE`, `NEXT`, `OBJECT_AITER`, `OBJECT_BIT_INVERT`, `OBJECT_DIR`, `OBJECT_DIVMOD`, `OBJECT_EQ`, `OBJECT_FORMAT`, `OBJECT_FORMAT_CONST`, `OBJECT_GE`, `OBJECT_GT`, `OBJECT_IS_INSTANCE`, `OBJECT_IS_SUBCLASS`, `OBJECT_ITER`, `OBJECT_LE`, `OBJECT_LEN`, `OBJECT_LT`, `OBJECT_MODULO`, `OBJECT_NE`, `OBJECT_NOT`, `OBJECT_POWER`, `OBJECT_REPR`, `OBJECT_TRUTH`, `OBJECT_TYPE`, `OBJECT_VARS`, `PAIR_SPLICE`, `POP`, `POP_COLLECT`, `POP_DYN_WITH`, `POP_FOREACH`, `POP_LOOP`, `POP_PROMPT`, `POP_SHIFT`, `POP_WIND`, `POP_WITH`, `PROPER`, `PUSH_DYN_WITH`, `PUSH_FOREACH`, `PUSH_NS`, `PUSH_PARAM`, `PUSH_WIND`, `PUSH_WITH`, `RAISE`, `RETURN`, `TAIL_CALL_NAMES`, `TAIL_CALL_STAR`, `UNPACK`, `UNPACK_FAST`, `UNPACK_STAR`, `UNPACK_VAR`, `VAR_ATTR_STORE`, `VAR_BIND`, `VAR_OBJECT_ADD`, `VAR_OBJECT_BIT_AND`, `VAR_OBJECT_BIT_LEFT`, `VAR_OBJECT_BIT_OR`, `VAR_OBJECT_BIT_RIGHT`, `VAR_OBJECT_BIT_XOR`, `VAR_OBJECT_FLOOR_DIVIDE`, `VAR_OBJECT_MATRIX_MULTIPLY`, `VAR_OBJECT_MODULO`, `VAR_OBJECT_MULTIPLY`, `VAR_OBJECT_POWER`, `VAR_OBJECT_SUBTRACT`, `VAR_OBJECT_TRUE_DIVIDE`, `VAR_STORE`, `YIELD`, `YIELD_FROM`

#### `KosContext_StackPushBlock`

- **Group:** Stack
- **Responsibility:** Allocate or push a new stack block segment on the current frame stack.
- **Implementation (current):** **Not** a standalone `KosEvalState_*` entry point; stack segments are created as part of **`kos_dyn_push_*`** / region setup. See **Implementation mapping**.
- **Wraps / replaces (conceptual):** `KosStackBlock_PushBlock`
- **Motivating opcodes (Part 1 heuristic):** `PUSH_COLLECT`, `PUSH_DYN_WITH`, `PUSH_FINALLY`, `PUSH_FOREACH`, `PUSH_LOOP`, `PUSH_TRY`, `PUSH_WITH`

#### `KosContext_StackDropBlock`

- **Group:** Stack
- **Responsibility:** Drop a stack block segment.
- **Implementation (current):** Same as push-block: no `KosEvalState_*` wrapper; drops happen inside dyn unwind / **`kos_dyn_transition`** paths. See **Implementation mapping**.
- **Wraps / replaces (conceptual):** `KosStackBlock_DropBlock`
- **Motivating opcodes (Part 1 heuristic):** `POP_COLLECT`, `POP_DYN_WITH`, `POP_FOREACH`, `POP_LOOP`, `POP_WITH`, `PUSH_DYN_WITH`, `PUSH_WITH`

#### `KosContext_GetFramePc`

- **Group:** Frame / PC
- **Responsibility:** Read or write `pc` / `acting_pc` for prepare and control transfers.
- **Wraps / replaces (conceptual):** `CURRENT_PC`, `CURRENT_ACTING_PC`, branch opcodes
- **Motivating opcodes (Part 1 heuristic):** `BREAK`, `CHECK_EXCEPTION`, `CONTINUE`, `ITER_NEXT`, `JUMP`, `JUMP_AND`, `JUMP_FALSE`, `JUMP_OR`, `JUMP_TRUE`, `LABEL`, `POP_DYN_WITH`, `POP_SHIFT`, `POP_WITH`, `POSITION`, `PUSH_CC`, `PUSH_DYN_WITH`, `PUSH_FOREACH`, `PUSH_LOOP`, `PUSH_SHIFT`, `PUSH_WITH`, `RETURN`

#### `KosContext_CurrentNamespace`

- **Group:** Namespaces
- **Responsibility:** Return the active `KosNamespace` for the current frame (`CURRENT_NS`).
- **Wraps / replaces (conceptual):** `CURRENT_NS`, `GLOBALS`, `LOCALS`
- **Motivating opcodes (Part 1 heuristic):** `BUILD_FUNCTION`, `CALL_FAST`, `CALL_FAST_ATTR`, `CALL_FAST_ATTR_NAMES`, `CALL_FAST_ATTR_STAR`, `CALL_FAST_NAMES`, `CALL_FAST_STAR`, `FAST`, `FAST_ATTR`, `FAST_ATTR_CLEAR`, `FAST_ATTR_REF`, `FAST_ATTR_STORE`, `FAST_BIND`, `FAST_CLEAR`, `FAST_OBJECT_ADD`, `FAST_OBJECT_BIT_AND`, `FAST_OBJECT_BIT_LEFT`, `FAST_OBJECT_BIT_OR`, `FAST_OBJECT_BIT_RIGHT`, `FAST_OBJECT_BIT_XOR`, `FAST_OBJECT_FLOOR_DIVIDE`, `FAST_OBJECT_MATRIX_MULTIPLY`, `FAST_OBJECT_MODULO`, `FAST_OBJECT_MULTIPLY`, `FAST_OBJECT_POWER`, `FAST_OBJECT_SUBTRACT`, `FAST_OBJECT_TRUE_DIVIDE`, `FAST_REF`, `FAST_STORE`, `LOCALS`, `POP_COLLECT`, `POP_NS`, `PUSH_COLLECT`, `PUSH_NS`, `UNPACK_FAST`

#### `KosContext_CallableCall`

- **Group:** Calls / install
- **Responsibility:** Install and run a Koskari callable (`KosContext_Call` / `KosContext_CallArgs` wrapper).
- **Wraps / replaces (conceptual):** `CALL*`, `TAIL_CALL*`, `kos_op_outcome_from_call_int`
- **Motivating opcodes (Part 1 heuristic):** `CALL`, `CALL_ATTR`, `CALL_ATTR_NAMES`, `CALL_ATTR_STAR`, `CALL_FAST`, `CALL_FAST_ATTR`, `CALL_FAST_ATTR_NAMES`, `CALL_FAST_ATTR_STAR`, `CALL_FAST_NAMES`, `CALL_FAST_STAR`, `CALL_NAMES`, `CALL_STAR`, `CALL_VAR`, `CALL_VAR_ATTR`, `CALL_VAR_ATTR_NAMES`, `CALL_VAR_ATTR_STAR`, `CALL_VAR_NAMES`, `CALL_VAR_STAR`, `TAIL_CALL`, `TAIL_CALL_NAMES`, `TAIL_CALL_STAR`

#### `KosContext_DynTop`

- **Group:** Dyn-chain / transfer
- **Responsibility:** Read or validate `KosContext_DYN_TOP` for block POP/CLEAR/PUSH operations.
- **Wraps / replaces (conceptual):** `POP_*`, `CLEAR_*`, `PUSH_*` block family, `KosContext_PLAN`
- **Motivating opcodes (Part 1 heuristic):** `CLEAR_FINALLY`, `CLEAR_FOREACH`, `CLEAR_LOOP`, `CLEAR_TRY`, `POP_CC`, `POP_COLLECT`, `POP_DYN_WITH`, `POP_FINALLY`, `POP_FOREACH`, `POP_LOOP`, `POP_NS`, `POP_PARAM`, `POP_PROMPT`, `POP_SHIFT`, `POP_TRY`, `POP_WIND`, `POP_WITH`, `PUSH_SHIFT`, `YIELD_FROM`

#### `KosContext_TransferPlan`

- **Group:** Dyn-chain / transfer
- **Responsibility:** Read, set, or clear `KosContext_PLAN` and related flags for shift/transfer.
- **Wraps / replaces (conceptual):** `POP_SHIFT`, `PUSH_SHIFT`, `kos_free_transfer_plan`, `KosContext_FLAGS`
- **Motivating opcodes (Part 1 heuristic):** `POP_SHIFT`, `PUSH_SHIFT`, `RETURN`

#### `KosContext_PendingReturn`

- **Group:** Exceptions / unwinding
- **Responsibility:** Access pending return value / parent-return flags during unwind.
- **Wraps / replaces (conceptual):** `KosContext_PENDING_RETURN_*`, `RETURN` paths
- **Motivating opcodes (Part 1 heuristic):** `POP_PROMPT`, `RETURN`

#### `KosContext_InjectedException`

- **Group:** Exceptions / unwinding
- **Responsibility:** Read or clear inject fields for throw/resume paths.
- **Wraps / replaces (conceptual):** `KosContext_INJECT_*`, `YIELD_FROM`
- **Motivating opcodes (Part 1 heuristic):** `YIELD_FROM`

#### `KosContext_BoundaryFlags`

- **Group:** Exceptions / unwinding
- **Responsibility:** Read/write `KosContext_FLAGS` for running/dead/suspend boundaries.
- **Wraps / replaces (conceptual):** `AWAIT`, `YIELD`, `RETURN`, `POP_PROMPT`
- **Motivating opcodes (Part 1 heuristic):** `AWAIT`, `POP_PROMPT`, `POP_SHIFT`, `PUSH_SHIFT`, `RETURN`, `YIELD`

#### `KosContext_OpOutcome`

- **Group:** Frame / PC
- **Responsibility:** Build `KosOpResult` via `EVAL_*` macros (`EVAL_NORMAL`, `EVAL_RAISE`, `EVAL_YIELD`, `EVAL_AWAIT`, `EVAL_RETURN`, `EVAL_PARENT_RETURN`, `EVAL_CONTROL_RETURNED`, `EVAL_FATAL`). `KosOpOutcome` / `KOS_OP_*` macros deleted in Phase 6.
- **Wraps / replaces (conceptual):** `EVAL_*` macros in `eval.h`; dispatched by `cg_handle_op_result` → `eval_event_*`.
- **Motivating opcodes (Part 1 heuristic):** `ATTR`, `ATTR_CLEAR`, `ATTR_REF`, `ATTR_STORE`, `AWAIT`, `BREAK`, `BUILD_ARGS`, `BUILD_CLASS`, `BUILD_FUNCTION`, `BUILD_KWDS`, `BUILD_KWDS_NAMES`, `CALL`, `CALL_ATTR`, `CALL_ATTR_NAMES`, `CALL_ATTR_STAR`, `CALL_FAST`, `CALL_FAST_ATTR`, `CALL_FAST_ATTR_NAMES`, `CALL_FAST_ATTR_STAR`, `CALL_FAST_NAMES`, `CALL_FAST_STAR`, `CALL_NAMES`, `CALL_STAR`, `CALL_VAR`, `CALL_VAR_ATTR`, `CALL_VAR_ATTR_NAMES`, `CALL_VAR_ATTR_STAR`, `CALL_VAR_NAMES`, `CALL_VAR_STAR`, `CAPTURE_CC`, `CAR`, `CDR`, `CHECK_EQ`, `CHECK_EXCEPTION`, `CHECK_GE`, `CHECK_GT`, `CHECK_IN`, `CHECK_IS`, `CHECK_IS_INSTANCE`, `CHECK_IS_NOT`, `CHECK_IS_SUBCLASS`, `CHECK_LE`, `CHECK_LT`, `CHECK_NE`, `CHECK_NOT_IN`, `CLEAR_FINALLY`, `CLEAR_FOREACH`, `CLEAR_LOOP`, `CLEAR_TRY`, `COMBINE_ARGS`, `COMBINE_KWDS`, `CONS`, `CONST`, `CONST_ITEM`, `CONST_ITEM_CLEAR`, `CONST_ITEM_REF`, `CONST_ITEM_STORE`, `CONTINUE`, `DEREF`, `DUP`, `EXCEPTION`, `EXCEPTION_CLEAR`, `EXCEPTION_MATCH`, `FAST`, `FAST_ATTR`, `FAST_ATTR_CLEAR`, `FAST_ATTR_REF`, `FAST_ATTR_STORE`, `FAST_BIND`, `FAST_CLEAR`, `FAST_OBJECT_ADD`, `FAST_OBJECT_BIT_AND`, `FAST_OBJECT_BIT_LEFT`, `FAST_OBJECT_BIT_OR`, `FAST_OBJECT_BIT_RIGHT`, `FAST_OBJECT_BIT_XOR`, `FAST_OBJECT_FLOOR_DIVIDE`, `FAST_OBJECT_MATRIX_MULTIPLY`, `FAST_OBJECT_MODULO`, `FAST_OBJECT_MULTIPLY`, `FAST_OBJECT_POWER`, `FAST_OBJECT_SUBTRACT`, `FAST_OBJECT_TRUE_DIVIDE`, `FAST_REF`, `FAST_STORE`, `GLOBALS`, `IMPORT`, `IMPORT_FROM`, `IMPORT_REL`, `ITEM`, `ITEM_CLEAR`, `ITEM_REF`, `ITEM_STORE`, `ITER_NEXT`, `JUMP`, `JUMP_AND`, `JUMP_FALSE`, `JUMP_OR`, `JUMP_TRUE`, `LABEL`, `LOCALS`, `NEXT`, `NOOP`, `OBJECT_ADD`, `OBJECT_AITER`, `OBJECT_BIT_AND`, `OBJECT_BIT_INVERT`, `OBJECT_BIT_LEFT`, `OBJECT_BIT_OR`, `OBJECT_BIT_RIGHT`, `OBJECT_BIT_XOR`, `OBJECT_DIR`, `OBJECT_DIVMOD`, `OBJECT_EQ`, `OBJECT_FLOOR_DIVIDE`, `OBJECT_FORMAT`, `OBJECT_FORMAT_CONST`, `OBJECT_GE`, `OBJECT_GT`, `OBJECT_IN`, `OBJECT_IS`, `OBJECT_IS_INSTANCE`, `OBJECT_IS_NOT`, `OBJECT_IS_SUBCLASS`, `OBJECT_ITER`, `OBJECT_LE`, `OBJECT_LEN`, `OBJECT_LT`, `OBJECT_MATRIX_MULTIPLY`, `OBJECT_MODULO`, `OBJECT_MULTIPLY`, `OBJECT_NE`, `OBJECT_NOT`, `OBJECT_NOT_IN`, `OBJECT_POWER`, `OBJECT_REPR`, `OBJECT_STR`, `OBJECT_SUBTRACT`, `OBJECT_TRUE_DIVIDE`, `OBJECT_TRUTH`, `OBJECT_TYPE`, `OBJECT_VARS`, `PAIR_SPLICE`, `PARAM`, `POP`, `POP_CC`, `POP_COLLECT`, `POP_DYN_WITH`, `POP_FINALLY`, `POP_FOREACH`, `POP_LOOP`, `POP_NS`, `POP_PARAM`, `POP_PROMPT`, `POP_SHIFT`, `POP_TRY`, `POP_WIND`, `POP_WITH`, `POSITION`, `PROPER`, `PUSH_CC`, `PUSH_COLLECT`, `PUSH_DYN_WITH`, `PUSH_FINALLY`, `PUSH_FOREACH`, `PUSH_LOOP`, `PUSH_NS`, `PUSH_PARAM`, `PUSH_PROMPT`, `PUSH_SHIFT`, `PUSH_TRY`, `PUSH_WIND`, `PUSH_WITH`, `RAISE`, `RETURN`, `TAIL_CALL`, `TAIL_CALL_NAMES`, `TAIL_CALL_STAR`, `THEEND`, `UNPACK`, `UNPACK_FAST`, `UNPACK_STAR`, `UNPACK_VAR`, `VAR`, `VAR_ATTR`, `VAR_ATTR_CLEAR`, `VAR_ATTR_REF`, `VAR_ATTR_STORE`, `VAR_BIND`, `VAR_CLEAR`, `VAR_OBJECT_ADD`, `VAR_OBJECT_BIT_AND`, `VAR_OBJECT_BIT_LEFT`, `VAR_OBJECT_BIT_OR`, `VAR_OBJECT_BIT_RIGHT`, `VAR_OBJECT_BIT_XOR`, `VAR_OBJECT_FLOOR_DIVIDE`, `VAR_OBJECT_MATRIX_MULTIPLY`, `VAR_OBJECT_MODULO`, `VAR_OBJECT_MULTIPLY`, `VAR_OBJECT_POWER`, `VAR_OBJECT_SUBTRACT`, `VAR_OBJECT_TRUE_DIVIDE`, `VAR_REF`, `VAR_STORE`, `YIELD`, `YIELD_FROM`

#### `KosContext_GlobalsFrame`

- **Group:** Namespaces
- **Responsibility:** Access outermost frame for `GLOBALS` / import paths.
- **Wraps / replaces (conceptual):** `KosContext_GLOBALS_FRAME`
- **Motivating opcodes (Part 1 heuristic):** `GLOBALS`, `IMPORT_REL`

### Intentionally not wrapped as `KosContext_*`

- **CPython runtime:** `Py*` APIs remain direct at opcode layer (reference counts, errors, `PyObject_*`).
- **Pure helpers** defined outside opcode handlers (`obj_op_*`, `push_result` implementation in `eval.c`): may become `static` friends of the context API or stay as helpers invoked by `KosContext_*` shims.
- **`insn` operand decoding:** remains `KosOp_*` on the handler’s `insn` parameter until a dedicated `KosContext_DecodeOperand` helper is introduced.

### Opcode coverage mapping (Part 1 → Part 2)

- **Total opcodes:** 207; **mapped to ≥1 API group in the Part 2 taxonomy:** 207 (per the original automated mapping).
- **Opcodes with no automated API mapping:** none.

**Current relevance:** Part 1 is **obsolete**, so this coverage statement describes the **historical** inventory only. For live code, use **`KosEvalState_*`** / **`kos_dyn_*`** / **`KosContext_*` macros** as in the Implementation mapping table.

Cross-reference: each opcode’s Part 1 section lists raw symbols from the old snapshot; Part 2 groups consolidate them under the original `KosContext_*` names, which map to `KosEvalState_*` and macros as above.
