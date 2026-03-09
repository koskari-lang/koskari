# Special Call Protocol (Kos Call)

## Revision (design clarifications)

This document reflects the **Koskari revision**: defer cookies and dedicated native frame kind; reuse KosFrame. Key principles:

- **No second frame type**: All resumable native execution uses a normal `KosFrame` activation.
- **No generalized mutable cookie**: Native handlers store state in the frame (stack, namespace) and optionally minimal frame fields. The frame `pc` is repurposed as the **native resume label** when in native mode.
- **Install slots own frame setup**: Every Kos-callable invocation goes through one of two type slots (`tp_kos_install` or `tp_kos_tail_install`). The install function performs all frame manipulation (push or reset), argument binding, and frame configuration. There is no split between "invocation selects" and "install configures"—the install does everything.
- **Native handler is frame evaluator**: If the top frame has `native_handler != NULL`, that handler *is* the frame's evaluator; signals (await result, yield send, nested-call return) are routed to it first.

---

## Implementation status

**Partially implemented.**

- **Implemented**: Install slots (`tp_kos_install`, `tp_kos_tail_install`) and unified invocation entry points (`kos_callable_call`, `kos_callable_tail_call`) as the canonical interface; single dispatch for CALL, CALL_NAMES, CALL_STAR, TAIL_CALL, TAIL_CALL_NAMES, TAIL_CALL_STAR; type-slot lookup; handler contract (signature, return 0/1/-1); install slots are the only path for KosCallable; non-KosCallable or KosCallable without install use default (Python call + push_or_await); KosFunction, KosExpression, DelimitedContinuation, Continuation, and apply all use install slots; native handler as frame evaluator; resume via `frame->pc` as native label; transfer plan model (context holds plan, eval loop drains it). Apply: 1/2/3-argument form, flattening when `fn` is KosFunction or KosExpression and `kwds` is NULL, else native frame with handler for `PyObject_Call` + push_or_await. AWAIT uses `kos_await_dispatch` (defer vs push_or_await). TAIL_* opcodes have explicit Plain Python paths (frame pop when parent exists, call, push); they return KOS_OP_RES_CONTROL_RETURNED when continuing to trigger kos_dynnode_on_control_returned for wind thunks.
- **Not implemented**: kos next protocol.

## Purpose

This document specifies a **unified protocol** for all callable invocations in the Koskari evaluator. Every call—whether a Koskari function, an expression, a continuation, a future meta-opcode (e.g. flattening `map`), a JIT-compiled function, or a plain Python callable—goes through the same interface. The handler for each callable type is responsible for setting up VM state and then telling the evaluator to continue, suspend, or error.

Goals:

- **Single code path**: The evaluator does not branch on callable type; it looks up a handler and invokes it.
- **One contract**: All handlers share the same signature and return convention.
- **Extensibility**: New callable kinds (optimized `apply`, flattening `map`, JIT functions) plug in by registering a handler; the evaluator does not change.
- **Dual convention**: Special callables remain normal Python callables (`tp_call`); when invoked from Koskari bytecode they may take an optimized path via the protocol.

---

## KosFrame shape and native execution

### Frame shape

A `KosFrame` may have:

- `code` **NULL** (pure native activation), in which case it **must** have a native handler installed, or
- `code` non-NULL (normal bytecode frame), and it may still host native execution (e.g. JIT frames may keep `code` for introspection/source mapping while executing natively).

### Using frame PC as native resume label

The frame's existing `pc` may be repurposed as the **native resume label** for re-entrant native execution. This avoids a separate cookie/label channel. The handler interprets `frame->pc` as a label/switch selector when running in native mode.

### Where native state lives

Native handlers should treat the frame as their activation record:

- **Operand stack**: temporary values, call results, etc.
- **Namespace/locals**: if convenient for named locals or debug visibility
- Optional minimal frame fields (if needed): e.g. `native_handler` pointer, `native_mode_flags`, `native_owner` for introspection.

State captured by continuations/templates is therefore captured by **frame cloning** (stack + namespace + `pc` + any additional frame fields), eliminating the need for cookie cloning.

---

## Install slots and invocation entry points

### Two slots: install and tail_install

`KosCallable` defines two type slots; there is no `is_tail` parameter—the slot identity implies the mode:

```c
typedef int (*KosInstallFunc)(
    KosContext *ctx,
    KosEvent   *out_ev,
    PyObject   *callable,
    PyObject   *args_tuple,
    PyObject   *kwds_dict   /* may be NULL */
);

typedef struct {
  PyTypeObject type;
  KosInstallFunc tp_kos_install;       /* non-tail; NULL => default (PyCall + push_or_await) */
  KosInstallFunc tp_kos_tail_install;  /* tail; NULL => no TCO; fallback to push + install */
} KosCallableTypeObject;
```

- **tp_kos_install** (non-tail): The install performs all frame setup, including pushing a new frame.
- **tp_kos_tail_install** (tail): The install performs all frame setup, including resetting the current frame for reuse. If NULL, the type does not support tail-call optimization; the invocation layer falls back to push + non-tail install.

### Install owns frame setup

Each install function is responsible for *all* frame manipulation:

1. Validate arity and argument shape.
2. Perform binding logic (e.g. Formals for function-like callables).
3. **Push** a new frame (non-tail) or **reset** the current frame (tail).
4. Configure the frame: set `code`, `ns`, and optionally `native_handler` for native execution.

The invocation layer does *not* push or reset; it only dispatches to the appropriate slot.

### Invocation entry points

The CALL and TAIL_CALL opcodes call the unified entry points:

```c
int kos_callable_call(KosContext *ctx, KosEvent *out_ev,
                      PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict);
int kos_callable_tail_call(KosContext *ctx, KosEvent *out_ev,
                           PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict);
```

**kos_callable_call**: Look up `tp_kos_install`. If non-NULL, invoke it. Else fall back to default (Python call + push_or_await).

**kos_callable_tail_call**: Look up `tp_kos_tail_install`. If non-NULL, invoke it. Else fall back: push a new frame via `kos_dyn_push_frame_empty`, then invoke `tp_kos_install` (no TCO).

### Return convention

Install returns 0 (continue), 1 (suspend, out_ev set), or -1 (error). On 0, the evaluator loop proceeds with the top frame (bytecode dispatch or native handler).

### Transfer plan integration

The context holds an active transfer plan (`KosContext_PLAN(ctx)`). At the top of each dispatch loop iteration, the evaluator drains the plan via `run_transfer_plan_to_completion`. An install that builds a transfer plan (e.g. continuation) sets `KosContext_PLAN(ctx) = plan` and returns 0. The evaluator loop takes over and drains the plan on the next iteration.

---

## Per-type install behavior

The following describes how each Koskari callable type implements the install slots. This is the canonical unified interface for invoking Koskari-aware callables.

**Stack ownership (tail_install)**: A tail_install handler (and any tail vinstall it uses) takes ownership of the caller's stack. The caller must not perform DropNoErr or any stack manipulation after the tail call returns. The handler clears or replaces the frame, invalidating the caller's stack reference.

### KosFunction

- **install**: Push a new frame. Use `KosFunction_PrepareCallNamespace` to bind formals from `(args_tuple, kwds_dict)`. Set `frame->code` and `frame->ns`. Set `frame->native_handler = NULL` (bytecode execution). Return 0.
- **tail_install**: Reset the current frame (clear stack, pc, native_handler, etc.). Apply formals and set code/ns as above. Return 0.

### KosExpression

- **install**: Push a new frame. Validate that `args_tuple` is empty and `kwds_dict` is NULL/empty. Set `frame->code` and `frame->ns` from the expression. Set `frame->native_handler = NULL`. Return 0.
- **tail_install**: Reset the current frame. Set code/ns as above. Return 0.

### DelimitedContinuation / Continuation

- **install**: Build the transfer plan (same-evaluator or cross-evaluator as appropriate). Set `KosContext_PLAN(ctx) = plan`. Return 0. The evaluator loop drains the plan on the next iteration.
- **tail_install**: May be NULL (no tail optimization for continuations) or same as install; continuation invocation semantics may not differentiate.

### Apply

Apply accepts 1, 2, or 3 arguments: `(fn)`, `(fn, arglist)`, or `(fn, arglist, kwds)`.

- **install (flattening)**: When `fn` is KosFunction or KosExpression and (for Expression) no args and no kwds, delegate to the inner callable's `tp_kos_install`: `KosCallableType_INSTALL(Py_TYPE(fn))(ctx, out_ev, fn, seq_args, kwds)`. The inner install pushes the frame.
- **install (non-flattening)**: Push a new frame. Set `frame->native_handler` to the apply handler. Push `fn`, `seq_args`, and `kwds` (if any) onto the frame stack for the handler. Set `frame->code = NULL`. Return 0. The evaluator runs the native handler, which does `PyObject_Call(fn, args, kwds)` and `push_or_await`.
- **tail_install**: Same logic; when flattening, delegate to inner `tp_kos_tail_install`; when not, reset the current frame and install the native handler.

---

## Native handler contract (frame evaluator)

When a frame has `native_handler != NULL`, that handler **is** the frame's evaluator. It is invoked by the evaluator loop (not by other handlers) and receives signals (await result, yield send, nested-call return, etc.).

### Signature (unified for call and resume)

```c
typedef int (*KosNativeFrameHandler)(
    KosContext *ctx,
    KosEvent *out_ev,
    int is_resume,
    PyObject *delivered_value);  /* only when is_resume != 0; new ref */
```

**Initial call** (`is_resume == 0`): The handler is invoked as the frame evaluator. The frame's `pc` may be used as an entry label.

**Resume** (`is_resume != 0`): The handler receives `delivered_value` (await result, yield send, nested-call return). The frame's `pc` acts as the **native resume label**; the handler interprets it to branch to the correct continuation point. No separate cookie is passed; the handler uses `frame->pc` (and possibly stack/namespace) for state.

### Return values

| Value | Meaning |
|-------|--------|
| `0` | **Continue**. The handler has left the context in a valid state. The evaluator runs the next frame or opcode. |
| `1` | **Suspend**. The handler has set `out_ev` and (if resumable) configured the frame for native resume (`pc` = resume label). The evaluator exits; on resume, the native handler is invoked again with the delivered value. |
| `-1` | **Error**. An exception is set; the evaluator propagates it. |

### Leaf mode vs activation mode (Tweak 1)

A kos-call handler may operate in one of two modes:

1. **Leaf mode (no activation frame)**: Permitted only if the handler will **not** require re-entry via `is_resume != 0`. Concretely: the handler will **not** suspend (return 1) and will **not** arrange to be resumed after a nested Koskari call returns. In leaf mode the handler does its work synchronously and returns 0 (continue) or -1 (error).

2. **Activation mode (uses a KosFrame)**: **Required** if the handler will ever need `is_resume != 0`. In practice, required if the handler will suspend (return 1) or push Koskari frame(s) for a nested call and later needs to continue native execution with that call's return value. In activation mode, the handler ensures there is a frame it owns as its activation and installs the native handler on that frame.

### Minimal VM rule: one pending native continuation per frame

Maintain **at most one pending native continuation per frame**, and make it **non-overwritable**. Installing a pending native handler on a frame must **error/assert** if one is already present. The evaluator clears the pending marker **immediately before** invoking it, preventing double-resume.

---

## Evaluator behavior

### Single path for CALL-family opcodes

All CALL-family opcodes reduce to:

1. Build canonical `(args_tuple, kwds_dict)`.
2. Invoke `kos_callable_call` (CALL) or `kos_callable_tail_call` (TAIL_CALL), which look up `tp_kos_install` or `tp_kos_tail_install` and dispatch.
3. Install returns 0 (continue), 1 (suspend), or -1 (error).
4. Evaluator runs the top frame (bytecode or native) or drains the transfer plan if one was installed.

There is no separate "begin argument" channel; argument handling is entirely the responsibility of the install function.

**Current implementation**: CALL, CALL_NAMES, and CALL_STAR use `kos_callable_call`. TAIL_CALL, TAIL_CALL_NAMES, and TAIL_CALL_STAR use `kos_callable_tail_call` (or `kos_callable_tail_vcall` for vectorcall paths). TAIL_* opcodes branch on KosCallable vs Plain Python: KosCallable uses tail install; Plain Python path performs `kos_dyn_pop_frame` when parent exists, then call, push. When the Plain Python path pushes and continues, opcodes return KOS_OP_RES_CONTROL_RETURNED to trigger `kos_dynnode_on_control_returned`.

### Resume precedence — native handler is the frame evaluator (Tweak 2)

If the **top frame** has a native handler installed, that handler is the frame's evaluator. Any delivered "signal" (await result, yield send value, throw injection, nested-call return, etc.) must be routed to the native handler first. The handler is **not** cleared before invocation; it remains installed until it explicitly uninstalls itself or the frame is popped. The frame `pc` acts as the native resume label when in native mode.

**Delivery checkpoints**: Resume after suspend revive (await, yield, throw, etc.); return to a parent frame after a child frame returns. At each checkpoint: if `top_frame->native_handler != NULL`, call it with the delivered signal. Else: proceed with normal bytecode dispatch.

### Native call discipline — enter native via the evaluator loop (Tweak 3)

Native handlers do **not** call other handlers directly. All native execution is entered and re-entered via the evaluator loop: the caller arranges for a frame to be installed/configured, then returns to the evaluator; the evaluator runs the top frame and, if it has a native handler, calls that handler as the frame evaluator. This ensures nesting is handled by the normal frame stack.

### Frame cloning provides multi-shot native capture (Tweak 4)

Native execution state must be fully represented by the frame (stack, namespace, `pc`, and any installed native handler + metadata). Therefore, continuation/template capture clones the frame and automatically snapshots native state. No generalized mutable cookie is required.

### Suspend and resume (summary)

When a handler causes a suspend: the opcode or native handler sets `out_ev` and returns 1; the evaluator exits. On resumption, the driver calls `koskari_eval`. Phase 1 runs revive (e.g. `revive_await`, `revive_yield`), which pushes the delivered value. If the **top frame** has `native_handler != NULL`, the evaluator invokes it with `(ctx, out_ev, 1, delivered_value)`. The handler interprets `frame->pc` as the resume label. The same mechanism applies for all suspend types and for return from nested calls.

---

## Native resume (no cookie; frame pc as label)

The native handler is stored on the **frame** (`frame->native_handler`), not as a separate pending continuation object. There is **no cookie**: the handler interprets `frame->pc` as the native resume label when re-entered. The evaluator checks the **top frame** for a native handler at delivery checkpoints (after revive or when the top frame changes after a return) and invokes it with the delivered value. **All suspend types** and nested-call returns use this same mechanism. Which frame holds the handler is **up to the handler implementation**: the handler decides whether it needs its own frame.

**Handler chooses the frame**: Utility callables (apply, map) typically use the **caller's** frame and install the native handler there. JIT or heavy native handlers push their **own** frame and install the handler on it. The evaluator only checks the top frame.

**Summary flow**: Native handler runs (is_resume=0), may push Koskari frames and return 0, or set `frame->pc` to a resume label and return 1 (suspend). On resume or return from nested call, the evaluator invokes the handler with `(ctx, out_ev, 1, delivered_value)`. The handler branches on `frame->pc` to the correct continuation point. The same handler is re-entered many times; `frame->pc` identifies the resume point.

---

## Kos next protocol (optional, for iterators)

A parallel protocol can be defined for **iteration** (NEXT, ITER_NEXT): a "kos next" handler on an iterator type that can produce the next value (or attach a frame) in a VM-native way. The evaluator would:

1. Before the existing KosPyAsyncGen / get_await_next / PyIter_Check logic, check whether the iterator's type has a kos next handler (slot or registry).
2. If yes, call it; if no, fall back to the current behavior.

This allows types like a flattening map iterator to be recognized without hard-coding them in the evaluator. The handler signature and return convention would mirror the existing next semantics (e.g. produce value, exhausted, or suspend). Details are left for a later extension of this document.

---

## Example: theoretically optimized flattening `map`

As a conceptual example of how the protocol supports meta-opcode-style optimization without hard-coding in the evaluator.

### Behavior

- **From Python**: `map(f, it)` is a normal call; the callable has `tp_call` that returns an iterator (lazy, element-by-element).
- **From Koskari**: When the CALL opcode sees this callable, the dispatcher invokes its kos call handler. The handler does not implement the full map loop in C. Instead it:
  1. Builds an iterator object (e.g. `KosMapIterator`) holding `(f, it)`.
  2. Pushes a **frame** whose execution represents the map loop: get next from `it` (via the normal next path, which may be kos next or KosPyAsyncGen or Python async/sync), then call `f` with that value (via the same kos call dispatcher, so `f` may be a KosFunction and get flattened), then push the result and loop or yield.
  3. Returns 0.

Any suspend happens inside that frame's execution (e.g. when "get next" or "call f" hits an awaitable). On resume, `revive_await` pushes the result onto that frame's stack and the next opcode in the frame uses it. So the "meta-opcode" is the frame's bytecode (or a small synthetic sequence), not a special case in the evaluator.

If the iterator type supports a **kos next** protocol, then when the user code does NEXT/ITER_NEXT on the map iterator, the evaluator can call that handler instead of the generic Python iterator path. The handler would: get next from the inner iterator (via the same next dispatcher), call `f` (via the call dispatcher), set the result and return GET_NEXT_VALUE (or suspend). Again, no hard-coding of "map" in the evaluator—only the generic "kos call" and "kos next" hooks.

---

## First real implementation: `apply`

The first concrete use of the special call protocol is an **apply** callable that accepts 1, 2, or 3 arguments: `fn`, `fn` and `arglist`, or `fn`, `arglist`, and `kwds`.

### Signature

| Arity | Form | Description |
|-------|------|-------------|
| 1 | `(apply fn)` | Call `fn` with no arguments. |
| 2 | `(apply fn arglist)` | Call `fn` with positional arguments from `arglist` (a sequence; pairs are unpacked as in existing apply). |
| 3 | `(apply fn arglist kwds)` | Call `fn` with positional args from `arglist` and keyword arguments from `kwds` (e.g. a mapping). |

So: `apply(fn)`, `apply(fn, arglist)`, `apply(fn, arglist, kwds)`.

### Behavior from Python

When invoked from Python (e.g. `apply(some_fn, (1, 2), {'key': val})`), the callable behaves like a simple function application: it is a normal Python callable that unpacks `arglist` and `kwds` and calls `fn(*arglist, **kwds)` (with the same unpacking semantics as the current builtin `apply` for pairs). No special VM behavior.

### Behavior from Koskari

When the CALL opcode invokes this callable via `kos_callable_call` or `kos_callable_tail_call`, the apply type's **install** or **tail_install** slot runs:

1. Parse the arguments (1, 2, or 3) from `args_tuple` to get `fn`, optional `arglist`, optional `kwds`.
2. **Flattening**: If `fn` is KosFunction or KosExpression (and for Expression: no args, no kwds), delegate to the inner callable's install or tail_install. The inner install pushes or resets the frame.
3. **Non-flattening**: Push (or tail-reset) a frame with `native_handler` set to the apply handler. Push `fn`, `seq_args`, and `kwds` onto the frame stack. Return 0. The evaluator runs the native handler, which performs `PyObject_Call` + `push_or_await`.

So from Koskari, `(apply fn arglist kwds)` is equivalent to calling `fn` directly when `fn` is a Koskari function or expression—apply delegates to the inner install. When `fn` is not flattenable, apply uses a native frame whose handler does Python application.

### Implementation notes

- The apply callable has `tp_call` (for Python) and `tp_kos_install` / `tp_kos_tail_install` (for the evaluator).
- The install maps the 1/2/3-argument convention: element 0 = `fn`, element 1 = `arglist`, element 2 = `kwds`. Omitted arguments are treated as empty args and no keywords.
- When flattening, apply delegates to the inner callable's install or tail_install, which performs frame push/reset and configuration.
- **Current implementation**: Uses `tp_kos_install` / `tp_kos_tail_install`; flattening when `fn` is exactly KosFunction or KosExpression and (for Expression) arity 0 and no kwds; non-flattening path uses a native frame whose handler does `PyObject_Call` + push_or_await and detaches the frame before returning. Arglist-to-tuple conversion must stay in sync between the kos handler and `KosUtils_Apply` (Python `tp_call`).

---

## Concerns and caveats

The following points are not implied by "planned work" elsewhere and are worth keeping in mind when maintaining or extending the implementation.

- **Flattening and subclasses**: Apply uses `KosFunction_CheckExact` and `KosExpression_CheckExact` to decide whether to flatten. Subtypes of KosFunction or KosExpression do not get the flattened path; they are invoked via `PyObject_Call` + push_or_await. If a future type should be flattenable via apply, either switch to a `Check` (subtype) predicate or add an explicit allowlist.
- **NULL slot fallback**: Any type that passes `KosCallable_Check` but has NULL in the relevant slot (`tp_kos_install` or `tp_kos_tail_install`) is treated as a normal Python callable: the default handler (Python call + push_or_await) is used. Document this so that new callable types do not assume "no slot" has a different meaning.
- **Type layout for slot macros**: `KosCallableType_INSTALL(typ)` and `KosCallableType_TAIL_INSTALL(typ)` (or equivalent) cast `typ` (a `PyTypeObject *`) to `KosCallableTypeObject *`. This is correct only for types defined as `KosCallableTypeObject` (same memory layout). Do not use these macros on other type layouts.
- **Duplicate arglist conversion**: The logic that converts `arglist` (pair, tuple, list, or other sequence) into a tuple lives in both the apply kos call handler (eval.c) and in `KosUtils_Apply` (utils.c). Changes to one (e.g. new sequence types, error messages) should be reflected in the other to avoid behavioral drift.

---

## Summary

- **Revision**: Defer cookies and dedicated native frame kind; reuse KosFrame. Frame `pc` is the native resume label; no cookie. Install slots own all frame setup (push or reset).
- **Two slots**: `tp_kos_install` (non-tail) and `tp_kos_tail_install` (tail). If tail_install is NULL, no tail-call optimization; fallback is push + install.
- **Unified invocation**: `kos_callable_call` and `kos_callable_tail_call` are the canonical entry points. CALL and TAIL_CALL opcodes invoke them. Install functions perform all frame manipulation and configuration.
- **Per-type behavior**: Function and Expression push/reset frames with code/ns; Continuation builds a transfer plan, sets `KosContext_PLAN`, returns; Apply delegates when flattening or installs a native frame with handler for Python call. The evaluator loop drains transfer plans at each iteration.
- **Native handler as frame evaluator**: If `top_frame->native_handler != NULL`, that handler is the frame's evaluator. Signals (await result, yield send, nested-call return) are routed to it first. Handler uses `frame->pc` as resume label. Handler not cleared before invocation.
- **Leaf vs activation mode**: Handlers that never need re-entry (no suspend, no nested-call resume) may run in leaf mode (no activation frame). Handlers that suspend or need nested-call resume must use activation mode (own frame with native_handler).
- **One pending native continuation per frame**: Non-overwritable; installing when one exists must error/assert. Evaluator clears immediately before invoking.
- **Frame cloning**: Continuation/template capture clones the frame; native state (stack, namespace, pc, handler) is automatically snapshotted. No cookie cloning needed.
- **apply**: Flattens by delegating to inner callable's install when `fn` is KosFunction or KosExpression (and for Expression, arity 0, no kwds); otherwise installs a native frame whose handler performs Python call + push_or_await.
- **map**: Future example of a meta-opcode-style callable and optional kos next iterator; no evaluator special cases.
