# Vectorcall Integration Design

This document describes the design for integrating CPython's vectorcall
convention (PEP 590) into the Koskari call and tail-call paths, eliminating
the transient tuple allocation that currently mediates every positional function
application. It also covers adding the `tp_vectorcall` slot to `KosFunction` so
that Python code can invoke Koskari functions through the vectorcall protocol
without routing through `tp_call`.

## Background: Current Call Sequence

### Stack Layout at Call Sites

The compiler (`compile/specials/call.py`) emits arguments left-to-right with
the callable deepest and the last positional argument at TOS:

```
stack (bottom → top):  [callable, arg_0, arg_1, ..., arg_{N-1}]
                                                        ^ TOS
```

### `CALL N` Opcode (CALL.c)

```c
args = KosStackBlock_PopTail(&KosDynFrame_STACK(frame_top), n);
callable = KosStackBlock_PopRef(&KosDynFrame_STACK(frame_top));
kos_callable_call(ctx, out_ev, callable, args, NULL);
Py_DECREF(callable);
Py_XDECREF(args);
```

`KosStackBlock_PopTail` allocates a fresh `PyTuple`, copies N borrowed
references into it, and returns it. That tuple is then consumed (by
`PrepareCallNamespace` for KosCallable, or by `PyObject_Call` for plain Python
callables) and immediately freed. The allocation and deallocation are pure
overhead — the contents of the tuple are already sitting contiguously in the
stack block's `objs[]` array.

`TAIL_CALL N` is identical in structure, routing to `kos_callable_tail_call`
instead.

### `CALL_NAMES N` Opcode (CALL_NAMES.c)

Stack layout: `[callable, pos_0..pos_N, kw_val_0..kw_val_M, names_tuple]`.
The opcode pops the names tuple, calls `KosStackBlock_PopTailDictFromNames` to
build a kwds dict from the M values below it, then `PopTail` for the N
positional args, then pops the callable. Two heap allocations result: the
positional tuple and the keyword dict.

### Dispatch: `kos_callable_call` / `kos_callable_tail_call`

Both functions inspect `Py_TYPE(callable)` for a `KosCallableTypeObject`
subtype. If the type carries a `tp_kos_install` (or `tp_kos_tail_install`)
slot, it is invoked directly with `(ctx, out_ev, callable, args_tuple,
kwds_dict)`. Otherwise `call_default_python_call` is used, which calls
`PyObject_Call(callable, args_tuple, kwds_dict)`.

### Existing Vectorcall-in-Place: `obj_op_reducing`

Operator opcodes already avoid tuple allocation for native reducing operators:

```c
PyObject *const *args = KosStackBlock_TailPtr(CURRENT_STACK(ctx), opa);
res = fn(args, (size_t) opa);
KosStackBlock_DropNoErr(&CURRENT_STACK(ctx), (size_t) opa);
```

`KosStackBlock_TailPtr` returns a pointer directly into the block's `objs[]`
without allocating or touching refcounts. The drop follows the call. This is
the pattern we generalize.

### Existing Vector Formals Path: `KosFormals_VectorApply`

`KosFormals_VectorApply(formals, namespace, args, nargs, names)` already
accepts a `PyObject *const *args` pointer and `size_t nargs` directly, applying
positional and keyword arguments to a namespace without building a tuple. It
uses `PyVectorcall_NARGS(nargs)` to extract the positional count, and `names`
(a tuple of keyword names) for keyword mapping — exactly the vectorcall
protocol. `KosFunction_PrepareCallNamespace` currently calls `KosFormals_Apply`
(the tuple path); a parallel `KosFunction_PrepareCallNamespaceFromVector` can
call `KosFormals_VectorApply` instead.

---

## Proposed Changes

### 1. `CALL N` and `TAIL_CALL N`: TailPtr + Drop

Replace `PopTail` with `TailPtr` + post-call `Drop`:

```c
/* Before: */
args = KosStackBlock_PopTail(&KosDynFrame_STACK(frame_top), n);
callable = KosStackBlock_PopRef(&KosDynFrame_STACK(frame_top));
int r = kos_callable_call(ctx, out_ev, callable, args, NULL);
Py_DECREF(callable); Py_XDECREF(args);

/* After: */
PyObject *const *args = (n > 0)
    ? KosStackBlock_TailPtr(&KosDynFrame_STACK(frame_top), n)
    : NULL;
if (n > 0 && ! args) return -1;
PyObject *callable = KosStackBlock_PeekAt_FromTop(
    &KosDynFrame_STACK(frame_top), n);   /* borrows callable below args */
if (! callable) return -1;
int r = kos_callable_vcall(ctx, out_ev, callable, args, n, NULL);
KosStackBlock_DropNoErr(&KosDynFrame_STACK(frame_top), n + 1);
/* push result happens inside kos_callable_vcall via push_or_await */
```

The key timing difference:

| Step | Tuple path | TailPtr path |
|------|-----------|--------------|
| 1 | PopTail(N) → allocate tuple | TailPtr(N) → borrow pointer |
| 2 | PopRef → pop callable | PeekAt(N) → borrow callable |
| 3 | call(callable, tuple) | vcall(callable, args, N) |
| 4 | push result | Drop(N+1) |
| 5 | Decref tuple + callable | push result |

In the TailPtr path the stack depth is unchanged during the call. The callable
and all argument slots remain live on the stack until step 4 (Drop), so there
is no window where a GC or re-entrant call could find dangling pointers. The
Drop step uses `DropNoErr` because at that point the call has already succeeded
or failed, and result ownership has been transferred.

#### COW Safety

`KosStackBlock_TailPtr` returns a pointer into the block's internal array.
This is safe across the call provided no operation modifies the block during
the call. For native Python callables dispatched through `call_default_python_call`,
the Koskari stack is not touched during the Python call. For KosCallable
subtypes, `kos_install_function` calls `KosFunction_PrepareCallNamespace` and
then `kos_dyn_push_frame` — neither of which touches the calling frame's stack.
A COW copy would only be triggered by a `PushRef` or `CopyForWrite` on the
same `KosStackBlock **` pointer, which does not occur in these paths.

If a future path is introduced that touches the caller's stack during dispatch,
that path must not use the TailPtr shortcut. The implementation should assert
or document this invariant.

#### Stack ownership for tail calls

When an opcode invokes `kos_callable_tail_vcall` for a KosCallable, the tail
install handler assumes full ownership of the caller's stack. The handler either
frees the current frame via `kos_tail_koscall` or resets it via
`kos_dynframe_reset_for_tail`. The opcode must NOT call
`KosStackBlock_DropNoErr` after `kos_callable_tail_vcall` in the KosCallable
branch. The plain Python path leaves the stack valid; the opcode must DropNoErr
before `kos_push_or_await`.

### 2. New Dispatch Function: `kos_callable_vcall`

Introduce a parallel dispatch entry point that carries the vectorcall signature
throughout:

```c
int kos_callable_vcall(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames);   /* NULL for positional-only */
```

Dispatch logic:

```c
int kos_callable_vcall(...) {
    if (KosCallable_Check(callable)) {
        KosVectorInstallFunc vfn = KosCallableType_VINSTALL(Py_TYPE(callable));
        if (vfn)
            return vfn(ctx, out_ev, callable, args, nargs, kwnames);
        /* Fallback: build tuple and use existing install slot */
        KosInstallFunc fn = KosCallableType_INSTALL(Py_TYPE(callable));
        if (fn) {
            PyObject *tup = PyTuple_New(nargs);
            /* fill tup from args... */
            int r = fn(ctx, out_ev, callable, tup, NULL);
            Py_DECREF(tup);
            return r;
        }
    }
    return call_default_python_vcall(ctx, out_ev, callable, args, nargs, kwnames);
}
```

And `call_default_python_vcall`:

```c
static int call_default_python_vcall(
    KosContext *ctx, KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs,
    PyObject *kwnames) {

    size_t nargsf = (size_t) nargs;
    if (kwnames)
        nargsf |= PY_VECTORCALL_ARGUMENTS_OFFSET;  /* if offset is usable */
    PyObject *result = PyObject_Vectorcall(callable, args, nargsf, kwnames);
    if (! result) return -1;
    return push_or_await(ctx, out_ev, result);
}
```

This replaces `PyObject_Call(callable, tuple, kwds)` with
`PyObject_Vectorcall(callable, args, nargs, kwnames)`, eliminating the tuple
for all plain Python callables as well.

### 3. New KosCallableTypeObject Slots

Extend `KosCallableTypeObject` with vectorcall install slots:

```c
/* In callable.h */

typedef int (*KosVectorInstallFunc)(
    struct KosContext *ctx,
    struct KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames);   /* NULL = positional only */

typedef struct {
    PyTypeObject type;
    KosInstallFunc tp_kos_install;
    KosInstallFunc tp_kos_tail_install;
    KosVectorInstallFunc tp_kos_vinstall;        /* new; may be NULL */
    KosVectorInstallFunc tp_kos_tail_vinstall;   /* new; may be NULL */
} KosCallableTypeObject;

#define KosCallableType_VINSTALL(typ)       \
    (((KosCallableTypeObject *) (typ))->tp_kos_vinstall)
#define KosCallableType_TAIL_VINSTALL(typ)  \
    (((KosCallableTypeObject *) (typ))->tp_kos_tail_vinstall)
```

The new slots default to NULL. Existing subtypes that do not populate them
fall back to the tuple path in `kos_callable_vcall`.

### 4. `KosFunction`: Vector Install Slots

Add `KosFunction_PrepareCallNamespaceFromVector`:

```c
/* In func.h */
PyObject *KosFunction_PrepareCallNamespaceFromVector(
    PyObject *func,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames);
```

Implementation calls `KosFormals_VectorApply` (which already exists and
accepts this signature) instead of `KosFormals_Apply`. Self-reference binding
is performed identically afterward.

The new vectorcall install slots for `KosFunctionType`:

```c
static int kos_vinstall_function(
    KosContext *ctx, KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs,
    PyObject *kwnames) {

    PyObject *frame_ns = KosFunction_PrepareCallNamespaceFromVector(
        callable, args, nargs, kwnames);
    if (! frame_ns) return -1;
    PyObject *fn_code = KosFunction_CODE(callable);
    if (kos_dyn_push_frame(ctx, (KosCode *) fn_code, frame_ns) == NULL) {
        Py_DECREF(frame_ns);
        return -1;
    }
    (void) out_ev;
    return 0;
}

static int kos_tail_vinstall_function(
    KosContext *ctx, KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs,
    PyObject *kwnames) {

    PyObject *frame_ns = KosFunction_PrepareCallNamespaceFromVector(
        callable, args, nargs, kwnames);
    if (! frame_ns) return -1;
    int r = kos_tail_koscall(ctx, (KosCode *) KosFunction_CODE(callable),
                             frame_ns);
    if (r < 0) Py_DECREF(frame_ns);
    (void) out_ev;
    return r;
}
```

`KosFunctionType` becomes:

```c
KosCallableTypeObject KosFunctionType = {
    .type = { ... },
    .tp_kos_install      = kos_install_function,
    .tp_kos_tail_install = kos_tail_install_function,
    .tp_kos_vinstall     = kos_vinstall_function,
    .tp_kos_tail_vinstall = kos_tail_vinstall_function,
};
```

### 5. `CALL_NAMES N`: kwnames Tuple Instead of Dict

The `CALL_NAMES` opcode currently builds a kwds `PyDict` from the names tuple
and the values on the stack. Under the vectorcall protocol, keyword arguments
are passed as a `(args_ptr, nargs, kwnames)` triple, where `kwnames` is a
tuple of keyword name strings and the values for those keywords immediately
follow the positional args in the `args` array.

The existing stack layout for `CALL_NAMES` is already in this form:
`[callable, pos_0..pos_N, kw_val_0..kw_val_M, names_tuple]`. After popping the
names tuple from TOS, `TailPtr(N + M)` covers positional and keyword values
contiguously, in the right order for vectorcall. The names tuple (already a
`PyObject *` of type tuple) can be passed directly as `kwnames`. No dict is
built.

The revised opcode sequence:

```c
/* Pop names tuple from TOS */
PyObject *kwnames = KosStackBlock_PopRef(stack);
Py_ssize_t nkw = (Py_ssize_t) PyTuple_GET_SIZE(kwnames);
Py_ssize_t npos = (Py_ssize_t) KosOp_ARG(insn);
Py_ssize_t ntotal = npos + nkw;

PyObject *const *args = KosStackBlock_TailPtr(stack, ntotal + 1);
/* args[0] = callable, args[1..npos] = positional, args[npos+1..] = kw values */
PyObject *callable = args[0];  /* borrowed */
args++;                        /* advance past callable */

int r = kos_callable_vcall(ctx, out_ev, callable, args, npos, kwnames);
KosStackBlock_DropNoErr(stack, ntotal + 1);
Py_DECREF(kwnames);
```

This eliminates both the positional tuple and the keyword dict. The names
tuple is already in the constants pool and is borrowed; only the pop incurs a
refcount operation.

Note: for subtypes that fall back to the tuple path, `kos_callable_vcall` must
reconstruct a dict from `(args + npos, nkw, kwnames)` before calling the
legacy install slot. This reconstruction is the same cost as the current path
and is only paid on the fallback.

### 6. `CALL_STAR`: No Change

`CALL_STAR` and `TAIL_CALL_STAR` handle splat arguments. The splat values may
be arbitrary iterables requiring `PySequence_Tuple` coercion, and the resulting
argument sequence is not contiguous in the stack. These opcodes already
produce a consolidated tuple and optional dict before calling `kos_callable_call`.
They are excluded from this optimization and continue to use the tuple path.

### 7. `PY_VECTORCALL_ARGUMENTS_OFFSET`: Offset Optimization

The vectorcall protocol reserves `args[-1]` as writable scratch space when
`PY_VECTORCALL_ARGUMENTS_OFFSET` is set in `nargsf`. This allows CPython's
method descriptors to temporarily store `self` there rather than allocating a
shifted array.

In the Koskari stack, after `TailPtr(N)` returns `args` pointing at
`objs[len - N]`, the slot `args[-1]` is `objs[len - N - 1]` — the callable
itself (since the stack layout is `[callable, arg_0, ..., arg_{N-1}]`). This
slot is live and its value would be overwritten momentarily during a bound
method call via the offset mechanism.

For the non-names path, setting `PY_VECTORCALL_ARGUMENTS_OFFSET` is therefore
safe only if:

1. The stack block has `refcount == 1` (not COW-shared), so the write
   does not corrupt a shared snapshot.
2. The callable slot is immediately below `args[0]` in the same block (no
   block boundary between callable and first arg).

Both conditions can be checked cheaply before the call. When satisfied, the
flag can be set, allowing CPython's internal fast paths for bound methods and
slot wrappers to avoid an extra allocation. This is an optional second-phase
optimization on top of the core TailPtr change.

---

## `KosFunction` as a Python-Visible Vectorcall Target

Beyond the internal evaluator benefit, there is value in registering
`KosFunction` as a vectorcall-capable type so that CPython's own call
machinery (and C extension code) can invoke Koskari functions without the
overhead of `tp_call` and its mandatory tuple.

### `tp_vectorcall` Offset and Flag

CPython's vectorcall protocol for user-defined types requires:

1. Setting `Py_TPFLAGS_HAVE_VECTORCALL` in `tp_flags`.
2. Setting `tp_vectorcall_offset` to the byte offset of a `vectorcallfunc`
   field inside the object struct.
3. The field at that offset must hold a `vectorcallfunc` pointer (or NULL,
   in which case CPython falls back to `tp_call`).

The `vectorcallfunc` signature is:

```c
typedef PyObject *(*vectorcallfunc)(
    PyObject *callable,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwnames);
```

To wire this up for `KosFunction`:

```c
/* In func.h, extend KosFunction struct */
typedef struct {
    PyObject_HEAD
    PyObject *code;
    PyObject *base_ns;
    PyObject *formals;
    PyObject *name;
    vectorcallfunc vectorcall;   /* new: points to kos_function_vectorcall */
} KosFunction;

#define KosFunction_VECTORCALL_OFFSET   \
    offsetof(KosFunction, vectorcall)
```

The vectorcall implementation routes to `KosFunction_Call` (which returns a
`KosCoroutine`), using `KosFormals_VectorApply` to bind arguments:

```c
static PyObject *kos_function_vectorcall(
    PyObject *callable,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwnames) {

    return KosFunction_VectorCall(callable, args, nargsf, kwnames);
}
```

Where `KosFunction_VectorCall` is a new public function in `func.h`:

```c
PyObject *KosFunction_VectorCall(
    PyObject *func,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwnames);
```

Wired into `KosFunctionType`:

```c
KosCallableTypeObject KosFunctionType = {
    .type = {
        ...
        .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL,
        .tp_vectorcall_offset = KosFunction_VECTORCALL_OFFSET,
        ...
    },
    ...
};
```

And during `KosFunction_New` / `KosFunction_NewFromFormals`, initialize the
field:

```c
fn->vectorcall = kos_function_vectorcall;
```

Setting it to NULL (or omitting the flag) is also valid and simply falls back
to `tp_call`; the field should be set to the function pointer from the start
since KosFunction always supports vectorcall.

### Version Gating

`Py_TPFLAGS_HAVE_VECTORCALL` is defined to `0` in `compat_36.h` and
`compat_37.h`, making the flag a no-op on those versions. `tp_vectorcall_offset`
is ignored when the flag is absent. No conditional compilation is needed: the
struct field can exist unconditionally (it simply wastes one pointer on older
Pythons), and the flag is already shimmed to a benign value. The field will
remain at its initialized value but CPython will not use it below 3.8.

---

## Impact on `CALL_ATTR` Family

`CALL_ATTR.c`, `CALL_ATTR_NAMES.c`, and `CALL_ATTR_STAR.c` perform attribute
lookup followed by a call. They currently also pass through `kos_callable_call`.
Applying the same TailPtr pattern is feasible but requires careful handling
because the attribute object itself is a temporary. These opcodes are a
follow-on concern and are left for a subsequent design pass.

---

## Summary of Changes

| Component | Change |
|-----------|--------|
| `callable.h` | Add `KosVectorInstallFunc` typedef; add `tp_kos_vinstall` and `tp_kos_tail_vinstall` to `KosCallableTypeObject`; add accessor macros |
| `callable.c` | Initialize new slots to NULL in `KosCallableType` |
| `eval.c` | Add `kos_callable_vcall` and `kos_callable_tail_vcall`; add `call_default_python_vcall`; retain old `kos_callable_call` for CALL_STAR and non-vector paths |
| `eval.h` | Declare `kos_callable_vcall` and `kos_callable_tail_vcall` |
| `func.h` | Add `vectorcall` field to `KosFunction`; add `KosFunction_VECTORCALL_OFFSET`; declare `KosFunction_VectorCall` and `KosFunction_PrepareCallNamespaceFromVector` |
| `func.c` | Implement `kos_function_vectorcall`, `kos_vinstall_function`, `kos_tail_vinstall_function`; set flag and offset in `KosFunctionType`; initialize `fn->vectorcall` in constructors |
| `opcode/CALL.c` | Replace `PopTail` + `PopRef` with `TailPtr` + `PeekAt` + `vcall` + `Drop N+1` |
| `opcode/TAIL_CALL.c` | Same, routing to `kos_callable_tail_vcall` |
| `opcode/CALL_NAMES.c` | Replace tuple + dict allocation with `TailPtr` + kwnames passthrough |
| `opcode/TAIL_CALL_NAMES.c` | Same |
| `opcode/CALL_STAR.c` | No change |
| `opcode/TAIL_CALL_STAR.c` | No change |
