# Special Call Protocol (Kos Call)

## Revision (design clarifications)

This document reflects the **Koskari revision**: defer cookies and dedicated native frame kind; reuse KosFrame. Key principles:

- **No second frame type**: All resumable native execution uses a normal `KosFrame` activation.
- **No generalized mutable cookie**: Native handlers store state in the frame (stack, namespace) and optionally minimal frame fields. The frame `pc` is repurposed as the **native resume label** when in native mode.
- **INSTALL step**: Every Kos-callable invocation performs a mandatory INSTALL step via a type slot (`tp_kos_install`); argument binding and frame configuration are INSTALL's responsibility.
- **Invocation selects, INSTALL configures**: The caller selects tail vs non-tail (frame reuse vs push); INSTALL configures the selected frame for bytecode or native execution.
- **Native handler is frame evaluator**: If the top frame has `native_handler != NULL`, that handler *is* the frame's evaluator; signals (await result, yield send, nested-call return) are routed to it first.

---

## Implementation status

**Partially implemented.**

- **Implemented**: Single dispatch for CALL and TAIL_CALL; type-slot lookup only (no registry); handler contract (signature, return 0/1/-1); default handler (Python call + push_or_await); KosFunction, KosExpression, DelimitedContinuation, Continuation, and apply as kos callables via `KosCallableTypeObject` and `tp_kos_call` / `tp_kos_tail_call`. The apply callable: 1/2/3-argument form, flattening when `fn` is KosFunction or KosExpression and `kwds` is NULL, else `PyObject_Call` + push_or_await. AWAIT uses a small `kos_await_dispatch` (defer vs push_or_await).
- **Not implemented**: INSTALL slot (`tp_kos_install`) and unified invocation entry points (KosCallable_Call, KosCallable_TailCall); native handler as frame evaluator; resume via `frame->pc` as native label (no cookie); CALL_NAMES, CALL_STAR, TAIL_CALL_NAMES, TAIL_CALL_STAR (still stubs); kos next protocol.

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

## INSTALL step and invocation entry points

### Principle: invocation selects, INSTALL configures

Tail-position knowledge exists at the invocation site (opcode/compiled call). Therefore:

- **Invocation** selects whether to reuse the current frame (tail) or push a new frame (non-tail).
- **INSTALL** configures the selected frame for execution.

### Two invocation entry points

The caller chooses one of two entry points:

- `KosCallable_Call(...)` (non-tail): Allocate a new callee `KosFrame`; push it (becomes `ctx->top_frame`).
- `KosCallable_TailCall(...)` (tail): Reuse the current frame; **clear/reset** it to a pristine state suitable for replacement; it remains `ctx->top_frame`.

The only semantic difference is frame selection/reuse.

### Tail-call clearing/reset

Before INSTALL, tail-call must reset the current frame at minimum:

- operand stack contents / blocks
- exception slot
- any per-frame pending state that should not survive replacement
- `pc` (to an entry label)
- installed native handler (unless explicitly allowed; default: clear)

INSTALL will then populate the new namespace/bindings and execution engine.

### Mandatory INSTALL step via KosCallable install slot

Before a callable's frame executes (bytecode or native), the VM performs an **INSTALL** step. INSTALL is not hardcoded to `Formals`. Instead, `KosCallable` defines a type slot:

```c
typedef int (*KosInstallFunc)(
    KosContext *ctx,
    KosEvent   *out_ev,
    PyObject   *callable,
    PyObject   *args_tuple,
    PyObject   *kwds_dict,   /* may be NULL */
    int         is_tail
);
```

The evaluator invokes this install function for the callable's type.

**Install slot responsibilities**:

1. Validate arity and argument shape.
2. Perform any binding logic (e.g. using `Formals` for function-like callables).
3. Allocate/configure the callee frame as needed.
4. Select the execution engine for that frame: bytecode (`frame->code != NULL`), or native (`frame->native_handler != NULL`), or hybrid/JIT (both present, with native taking precedence at runtime).

**Formals as one install strategy**: For function-like callables, the callable carries `Formals` and INSTALL uses them to bind `(args_tuple, kwds_dict)` into the frame namespace. For other callables (apply, map, JIT, utility types), INSTALL may implement custom binding logic or push a native-configured frame directly.

### Return-to-loop rule

Invocation performs **no execution** beyond frame selection + INSTALL. It then returns to the evaluator loop, which begins execution based on the top frame's configured engine:

- if `native_handler != NULL`: native handler runs as frame evaluator
- else: bytecode dispatch runs

### Lookup (current implementation, pre-INSTALL)

Until INSTALL is implemented, the evaluator continues to use `tp_kos_call` / `tp_kos_tail_call` via `KosCallableTypeObject`. The INSTALL design unifies these into a single `tp_kos_install` plus the invocation entry points above. Types without an install slot fall back to the default handler (Python call + push_or_await).

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

### Single path for CALL-family opcodes (INSTALL design)

All CALL-family opcodes reduce to:

1. Build canonical `(args_tuple, kwds_dict)`.
2. Lookup `tp_kos_install` on the callable's type.
3. Invoke INSTALL (or fall back to default handler if no slot).
4. Evaluator runs the top frame (bytecode or native).

There is no separate "begin argument" channel; argument handling is entirely the responsibility of INSTALL.

**Current implementation (pre-INSTALL)**: CALL and TAIL_CALL use `kos_call_dispatch` with `tp_kos_call` / `tp_kos_tail_call`. CALL_NAMES, CALL_STAR, and the TAIL_* variants are not yet wired to the unified path.

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

When the CALL opcode invokes this callable (via the unified dispatcher or, in the INSTALL design, via the apply type's **INSTALL** slot), the handler performs custom binding:

1. Interprets the arguments (1, 2, or 3) from the `args` tuple (and kwds) to get `fn`, optional `arglist`, optional `kwds`.
2. If `fn` is a type that supports **flattening** (e.g. `KosFunction` or `KosExpression`), the handler does **not** call `fn` via PyObject_Call. Instead it prepares the call namespace from `arglist` and `kwds` and either:
   - Pushes a new frame for `fn`'s code with that namespace (normal call), or
   - Reuses the current frame (tail call) and configures it with `fn`'s code and namespace, matching the current opcode (CALL vs TAIL_CALL).
3. If `fn` is not flattenable (e.g. a Python callable), the handler falls back to normal application: build the combined args/kwds and call the default handler (or PyObject_Call + push_or_await) for `fn`.

So from Koskari, `(apply fn arglist kwds)` is equivalent to calling `fn` directly with those arguments when `fn` is a Koskari function or expression—the evaluator flattens into the current context and runs `fn`'s code directly. When `fn` is not flattenable, behavior is the same as from Python (simple application). Under the INSTALL design, apply implements custom binding logic in its install slot rather than using Formals.

### Implementation notes

- The apply callable is a new type (e.g. `KosApplyType`) that has both `tp_call` (for Python) and a registered kos call handler (for the evaluator).
- The handler must map the 1/2/3-argument convention from the CALL opcode's `args` (and any kwds) into the internal (fn, arglist, kwds) form. The compiler will emit CALL with 1, 2, or 3 positional arguments; the handler receives the tuple and interprets element 0 as `fn`, element 1 (if present) as `arglist`, element 2 (if present) as `kwds`. Omitted arguments are treated as no extra args and no keywords (equivalent to `()` and `None` or empty mapping).
- The handler can reuse the same namespace-preparation and frame-push/tail-replace logic as the existing KosFunction and KosExpression handlers, so unification allows apply to "forward" into the same code paths without duplicating them.

**Current apply implementation**: Flattening is used only when `fn` is exactly `KosFunction` or exactly `KosExpression` (e.g. `KosFunction_CheckExact(fn)`), and for expressions only when the positional arglist is empty and `kwds` is NULL. If `kwds` is non-NULL (including an empty dict), the handler does not flatten and uses `PyObject_Call(fn, seq_args, kwds)` then push_or_await, because the flattened call path does not accept keyword arguments. Arglist-to-tuple conversion is implemented in both the kos call handler (eval) and in `KosUtils_Apply` (Python `tp_call`); the two must stay in sync.

---

## Concerns and caveats

The following points are not implied by "planned work" elsewhere and are worth keeping in mind when maintaining or extending the implementation.

- **Flattening and subclasses**: Apply uses `KosFunction_CheckExact` and `KosExpression_CheckExact` to decide whether to flatten. Subtypes of KosFunction or KosExpression do not get the flattened path; they are invoked via `PyObject_Call` + push_or_await. If a future type should be flattenable via apply, either switch to a `Check` (subtype) predicate or add an explicit allowlist.
- **NULL handler/install fallback**: Any type that passes `KosCallable_Check` but has NULL in the relevant slot (`tp_kos_call`/`tp_kos_tail_call` today, or `tp_kos_install` in the INSTALL design) is treated as a normal Python callable: the default handler is used. Document this so that new callable types do not assume "no slot" has a different meaning.
- **Type layout for slot macros**: `KosCallableType_CALL(typ)` and `KosCallableType_TAIL_CALL(typ)` cast `typ` (a `PyTypeObject *`) to `KosCallableTypeObject *`. This is correct only for types defined as `KosCallableTypeObject` (same memory layout). Do not use these macros on other type layouts.
- **Duplicate arglist conversion**: The logic that converts `arglist` (pair, tuple, list, or other sequence) into a tuple lives in both the apply kos call handler (eval.c) and in `KosUtils_Apply` (utils.c). Changes to one (e.g. new sequence types, error messages) should be reflected in the other to avoid behavioral drift.

---

## Summary

- **Revision**: Defer cookies and dedicated native frame kind; reuse KosFrame. Frame `pc` is the native resume label; no cookie. INSTALL step configures the selected frame; invocation selects tail vs non-tail.
- **One path (INSTALL design)**: CALL-family opcodes build (args_tuple, kwds_dict), invoke `tp_kos_install`, then evaluator runs top frame. Currently CALL/TAIL_CALL use `tp_kos_call` / `tp_kos_tail_call`; INSTALL not yet implemented.
- **Invocation entry points**: `KosCallable_Call` (non-tail: push new frame) and `KosCallable_TailCall` (tail: reuse + reset current frame). INSTALL configures the selected frame for bytecode or native execution.
- **Native handler as frame evaluator**: If `top_frame->native_handler != NULL`, that handler is the frame's evaluator. Signals (await result, yield send, nested-call return) are routed to it first. Handler uses `frame->pc` as resume label. Handler not cleared before invocation.
- **Leaf vs activation mode**: Handlers that never need re-entry (no suspend, no nested-call resume) may run in leaf mode (no activation frame). Handlers that suspend or need nested-call resume must use activation mode (own frame with native_handler).
- **One pending native continuation per frame**: Non-overwritable; installing when one exists must error/assert. Evaluator clears immediately before invoking.
- **Frame cloning**: Continuation/template capture clones the frame; native state (stack, namespace, pc, handler) is automatically snapshotted. No cookie cloning needed.
- **apply**: First real implementation; from Koskari it flattens when `fn` is exactly KosFunction or KosExpression and (for expressions) arity 0 and no kwds; otherwise behaves like simple application. With INSTALL, apply implements custom binding in its install slot.
- **map**: Future example of a meta-opcode-style callable and optional kos next iterator; no evaluator special cases.
