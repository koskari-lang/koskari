# Special Call Protocol (Kos Call)

## Implementation status

**Partially implemented.**

- **Implemented**: Single dispatch for CALL and TAIL_CALL; type-slot lookup only (no registry); handler contract (signature, return 0/1/-1); default handler (Python call + push_or_await); KosFunction, KosExpression, DelimitedContinuation, Continuation, and apply as kos callables via `KosCallableTypeObject` and `tp_kos_call` / `tp_kos_tail_call`. The apply callable: 1/2/3-argument form, flattening when `fn` is KosFunction or KosExpression and `kwds` is NULL, else `PyObject_Call` + push_or_await. AWAIT uses a small `kos_await_dispatch` (defer vs push_or_await).
- **Not implemented**: Registry-based handler lookup; resume (`is_resume != 0`) and pending native continuation (Phase 1.5, frame-stored handler+cookie); CALL_NAMES, CALL_STAR, TAIL_CALL_NAMES, TAIL_CALL_STAR (still stubs); kos next protocol.

## Purpose

This document specifies a **unified protocol** for all callable invocations in the Koskari evaluator. Every call—whether a Koskari function, an expression, a continuation, a future meta-opcode (e.g. flattening `map`), a JIT-compiled function, or a plain Python callable—goes through the same interface. The handler for each callable type is responsible for setting up VM state and then telling the evaluator to continue, suspend, or error.

Goals:

- **Single code path**: The evaluator does not branch on callable type; it looks up a handler and invokes it.
- **One contract**: All handlers share the same signature and return convention.
- **Extensibility**: New callable kinds (optimized `apply`, flattening `map`, JIT functions) plug in by registering a handler; the evaluator does not change.
- **Dual convention**: Special callables remain normal Python callables (`tp_call`); when invoked from Koskari bytecode they may take an optimized path via the protocol.

---

## Handler lookup (no branching on type)

The evaluator does **not** use an if/else chain on callable type. Instead it resolves a handler in one of two ways:

1. **Type slot**: The callable's type has an optional slot (e.g. `tp_kos_call` or a small extension struct). If the slot is non-NULL, the evaluator calls it.
2. **Registry**: A table maps `PyTypeObject *` to a C function pointer. The evaluator looks up `Py_TYPE(callable)`; if found, calls that handler; otherwise calls the **default handler**.

The default handler implements normal Python invocation: `PyObject_Call(callable, args, kwds)` (or equivalent) and then `push_or_await` for the result. So any type without a registered handler behaves as a normal Python callable.

"Type-checking" in the evaluator is therefore a single lookup and one indirect call—no special cases for KosFunction, KosExpression, continuations, or future types.

### Current implementation (C)

- **Lookup**: Type slot only. No registry. Callable types extend a base whose C struct is `KosCallableTypeObject` (a `PyTypeObject` plus `tp_kos_call` and `tp_kos_tail_call`). The evaluator uses `KosCallable_Check(callable)` (subtype of base) then reads `KosCallableType_CALL(Py_TYPE(callable))` or `KosCallableType_TAIL_CALL(Py_TYPE(callable))`. The slot macros cast `PyTypeObject *` to `KosCallableTypeObject *`; they are valid only for types whose definition is that struct (or has the same layout for the first three fields).
- **Dispatcher**: `kos_call_dispatch(ctx, out_ev, callable, args, is_tail)`. If the type has a non-NULL handler for the requested tail/non-tail slot, that handler is called with `(ctx, out_ev, 0, callable, args)`. Otherwise the default handler is used. CALL and TAIL_CALL opcodes pop callable and args then call this dispatcher; CALL_NAMES, CALL_STAR, TAIL_CALL_NAMES, TAIL_CALL_STAR do not yet use it.
- **Default**: A subtype of the callable base that leaves `tp_kos_call` / `tp_kos_tail_call` NULL still passes `KosCallable_Check`; the dispatcher then calls the default handler (Python call + push_or_await), so such types behave as normal Python callables from bytecode.

---

## Kos call handler contract

### Signature (unified for call and resume)

Every kos call handler uses the **same** signature for both the initial call and for resume after any suspend (await, yield, etc.). The evaluator invokes the same function pointer in both cases; a flag indicates which phase.

```c
typedef int (*KosCallHandler)(
    KosContext *ctx,
    KosEvent *out_ev,
    int is_resume,
    PyObject *arg1,
    PyObject *arg2);
```

**Initial call** (`is_resume == 0`):

- `arg1`: The callable being invoked (borrowed reference). Same object that would be used for `tp_call` from Python.
- `arg2`: Positional arguments as a tuple (borrowed reference), or NULL if arity is 0. For opcodes that carry keyword or star-args (CALL_NAMES, CALL_STAR, etc.), the dispatcher must pass a single representation (e.g. tuple of (args, kwds) or similar); the exact convention is chosen at implementation time.

**Resume after suspend** (`is_resume != 0`):

- `arg1`: The delivered value (result of an await, or the value sent into a yield, or the return value from a nested call—see below). New reference; the handler must Py_DECREF when done or pass it on. The same continuation mechanism is used for **all** suspend types (await, yield, etc.); whenever the evaluator resumes and delivers a value to the frame, it may invoke the pending native continuation with that value.
- `arg2`: Cookie (opaque PyObject*, e.g. a capsule or small object holding a resume label/index). Same object the handler stored when it set the pending native continuation. The handler uses it to branch to the correct continuation point (e.g. jump table or switch).

The handler **must not** retain references to `arg1` or `arg2` after return when `is_resume == 0`; the opcode handler (or dispatcher) is responsible for DECREF. On resume, the handler owns the interpretation of `arg1` and `arg2` (typically consumes or stores the result and uses the cookie to continue).

**Tail call**: The evaluator passes a separate flag or uses a separate slot/registry entry for tail (replace current frame instead of pushing). The handler signature above is unchanged; tail semantics are indicated by the dispatcher call site, not by the arguments.

**Parameter summary**:

- `ctx`: The current evaluation context. The handler may mutate it (push/pop frames, push values, set transfer plan, push dyn nodes).
- `out_ev`: Output event. The handler must set `KosEvent_KIND(out_ev)` and optionally `KosEvent_PAYLOAD(out_ev)` when returning 1 (suspend).
- `is_resume`: 0 = initial call (arg1=callable, arg2=args); non-zero = resume (arg1=resumed value, arg2=cookie).
- `arg1`, `arg2`: Meaning depends on `is_resume` as above.

### Return values

| Value | Meaning |
|-------|--------|
| `0` | **Continue**. The handler has left the context in a valid state. The evaluator will run the next opcode (possibly in a new frame the handler pushed). |
| `1` | **Suspend**. The handler has set `out_ev` (e.g. `KOS_EV_AWAIT`, `KOS_EV_YIELD`, etc.) and possibly a **pending native continuation** on the frame it uses (for JIT/native bodies that must be re-entered). The evaluator exits the loop and returns to the caller; resumption runs the appropriate revive (await, yield, etc.), then either continues the opcode loop or invokes the pending native continuation from the top frame with the delivered value (see **Native resume (pending continuation)**). The same continuation is used for **all** suspend types. |
| `-1` | **Error**. An exception is set; the evaluator propagates it. |

### What the handler may do

The handler is allowed to:

- Push one or more frames (e.g. prepare a call namespace and push a KosCode frame, as today's `call_native_flattened_function` does).
- Push values onto the current (or top) frame's stack.
- Replace the current frame (tail call) instead of pushing.
- Set a transfer plan or push dyn nodes, if the callable semantics require it (e.g. continuations).
- Set a **pending native continuation** (same handler + cookie) on whichever frame the handler is using as its activation (see below) and return 1 (suspend).

**Two continuation styles** are supported:

1. **Frame-based**: The handler pushes a frame; any suspend happens inside that frame's bytecode. On resume, the evaluator continues at the next opcode in that frame. The handler is not re-entered.
2. **Native resume**: The handler (or code it invokes) runs a native block of code. When it needs to await or call into Koskari, it sets a **pending native continuation** on the frame it is using as its activation (handler + cookie). On suspend it returns 1; when calling into Koskari it may push new frame(s) and return 0. When the evaluator later resumes (after await) or returns to that frame (after nested Koskari calls complete), it invokes the continuation with the resumed or return value. See **Native resume (pending continuation)** below.

---

## Evaluator behavior

### Single dispatch for CALL-family opcodes

All opcodes that invoke a callable (CALL, CALL_NAMES, CALL_STAR, TAIL_CALL, TAIL_CALL_NAMES, TAIL_CALL_STAR, and the RECUR/TAIL_RECUR family) should follow the same path. **Currently only CALL and TAIL_CALL use the unified dispatcher** (`kos_call_dispatch`); CALL_NAMES, CALL_STAR, and the TAIL_* variants are not yet wired to it.

Intended path:

1. Pop arguments and callable from the stack according to the opcode (e.g. CALL N pops N args then callable).
2. Call the unified dispatcher, e.g. `kos_call_dispatch(ctx, out_ev, callable, args, tail_p)`. The `tail_p` flag selects normal vs tail semantics. Keyword and star-arg variants will pass the appropriate representation of args/kwds when implemented.
3. If the dispatcher returns 0, the evaluator continues the loop (next opcode). If 1, the evaluator exits with the event. If -1, the evaluator propagates the error.

The dispatcher's logic is:

1. Resolve the handler for `Py_TYPE(callable)` (slot or registry). If none, use the default handler (Python call + push_or_await).
2. Call the resolved handler with `(ctx, out_ev, 0, callable, args)` (and tail semantics when `tail_p` is true).
3. Return the handler's return value.

No per-type branching: one lookup, one call.

### Suspend and resume

When a handler (or code run inside a frame it pushed) causes a suspend:

- The opcode handler that ultimately ran sets `KosEvent_KIND(out_ev)` (e.g. `KOS_EV_AWAIT`, `KOS_EV_YIELD`) and `KosEvent_PAYLOAD(out_ev)` appropriately, and returns 1.
- The evaluator records the suspension reason (e.g. `KOS_SUSP_AWAIT`, `KOS_SUSP_YIELD`) and exits.
- On resumption, the driver calls `koskari_eval(ctx, out_ev, send_val, throw_exc)`. Phase 1 runs the appropriate revive (e.g. `revive_await`, `revive_yield`), which pushes the delivered value (`send_val` or equivalent) onto the **current (top) frame's stack**.
- **Phase 1.5 — native resume**: If the **current (top) frame** has a **pending native continuation** set (see below), the evaluator does *not* simply continue the opcode loop. Instead it invokes the stored handler with the **delivered value** (the value revive just pushed—whether from an await, a yield send, or any other suspend type): call the handler with `(ctx, out_ev, 1, delivered_value, cookie)`, then clear the continuation on that frame. The handler returns 0 (continue loop), 1 (suspend again; it may set another pending continuation on the same frame), or -1 (error). If 0, the evaluator then continues the normal loop. **The same mechanism applies for all suspend types** (await, yield, etc.); the native continuation receives whatever value the evaluator delivers on resume.
- If the top frame has no pending native continuation, the evaluator continues the loop; the next opcode is in the same frame (PC had been advanced before the opcode that suspended). So for frame-based continuations, the resume is (frame, PC, stack).

Handlers that need to suspend (await, yield, etc.) can either encode the continuation as a frame (bytecode runs next) or set a pending native continuation so that a C (or JIT) entry point is called again with the delivered value.

---

## Native resume (pending continuation)

The pending native continuation (handler + cookie) is stored on a **frame**, not on the context. The evaluator always checks the **top frame** for a pending continuation (after any revive—await, yield, etc.—or when control returns to a frame). When present, it is invoked with the value delivered to that frame (await result, yield send value, or return value from nested call). **All suspend types** (await, yield, and any future boundary that delivers a value on resume) use this same mechanism; the handler does not need to distinguish. Which frame holds the continuation is **up to the handler implementation**: the handler decides whether it needs its own frame and therefore on which frame it installs the continuation.

### Handler chooses the frame

- **No frame pushed (e.g. utility callables like apply, map)**: The handler runs in the context of the **caller's** frame—the frame that invoked the callable. It does not push a new frame unless it needs to make a nested call (then it pushes frame(s) for that call and returns 0). Any pending native continuation (for suspend or for "resume when nested call returns") is stored on the **caller's** frame. So the utility is responsible for installing itself on the caller frame; when the evaluator later sees that frame as top (after revive or after nested frames pop), it will find the continuation and invoke it.
- **Frame pushed (e.g. JIT or heavy native)**: The handler may push its own frame for storage (dyn nodes, value stack, etc.) and run without per-opcode dispatch. When it suspends or calls into Koskari, it stores the pending continuation on **its own** frame. When those nested frames pop or when we resume after await, the top frame is that frame and the evaluator invokes the continuation there.

So the **implementation decides**: leave the caller frame in place and use it for the continuation, or push a frame and use that. The protocol does not mandate one or the other; the evaluator simply checks the top frame for a pending continuation whenever it matters (Phase 1.5 after revive, or when the top frame changes after a return).

### Storing on the frame

Each frame may have an optional **pending native continuation**: the same handler (same `KosCallHandler` as for the initial call) and a cookie (PyObject*, e.g. a capsule or small object encoding resume index, label id, or PC offset). For example, `frame->pending_native_resume_handler` and `frame->pending_native_resume_cookie`, or a single object that encapsulates both. The handler sets this on the frame it is using as its activation (caller's frame or its own pushed frame) before either suspending (return 1) or calling into Koskari (push new frame(s), return 0). When that frame is popped, the continuation is discarded with it—no separate clear on the context.

### Two ways the continuation is invoked

1. **After any suspend (await, yield, etc.)**: The native body set the continuation on the frame it uses as its activation and returned 1 (suspend). On the next `koskari_eval` entry, phase 1 runs the appropriate revive (e.g. `revive_await`, `revive_yield`), which pushes the delivered value onto the top frame's stack. Phase 1.5 runs: if the **top frame** has a pending native continuation, the evaluator calls the stored handler with `(ctx, out_ev, 1, delivered_value, cookie)`, then clears the continuation on that frame. The delivered value is whatever the suspend type provides (await result, yield send value, etc.). The handler continues (e.g. branch on cookie), may run to completion (return 0), suspend again (set continuation on the same frame, return 1), or error (return -1). **The same continuation is used for all suspend types**; the handler receives the value and need not care whether it came from await or yield.

2. **After return from nested Koskari calls**: The native body needed to call a Koskari function. It set the continuation on the frame it uses as its activation (handler + cookie indicating "resume here with the call's return value"), pushed the new frame(s) for the Koskari call, and returned 0. The evaluator loop continued and ran the new frame(s). When those frames complete (RETURN), the evaluator pops back to the frame that holds the continuation (caller's frame or the native's own frame). So when the top frame changes after a return, the evaluator checks: does the **new** top frame have a pending native continuation? If yes, invoke it with the return value (the value just returned from the nested call) as `arg1` and the stored cookie as `arg2`, then clear the continuation. The native code then continues with that value and may push more frames, suspend, or complete.

So the same mechanism (continuation on the frame, invoked with a value) covers resume after **any** suspend (await, yield, etc.) and resume after nested call returns. The evaluator does not need to know which case it is; it only checks the top frame for a pending continuation and, if present, calls the handler with the appropriate value (from revive or from the return path).

### Summary flow

- Native handler runs (is_resume=0). It may set continuation on the frame it uses (caller's or its own), push new Koskari frames, return 0 → evaluator runs the new frames; when they return, we pop back to the frame that has the continuation, invoke it (is_resume=1, arg1=return value, arg2=cookie).
- Or it sets continuation on that frame and returns 1 (suspend) → evaluator exits; on next entry, revive pushes value, Phase 1.5 sees top frame's continuation, calls handler (is_resume=1, arg1=resumed value, arg2=cookie).
- The same handler is re-entered many times; the cookie identifies the resume point. The handler implementation chooses which frame holds the continuation; the evaluator only checks the top frame.

**Utility (e.g. apply, map)**: Such handlers typically do not push their own frame. They run in the caller's frame and install any continuation on the **caller's** frame. When they need to make a nested call, they push frame(s) for it and return 0; when those frames pop, the caller frame is again top and its continuation runs with the return value.

**JIT example**: A JIT-compiled function may push its **own** frame (for storage, dyn nodes, value stack) and run without per-opcode dispatch. The handler (is_resume=0) pushes that frame and calls into the native body. The native body runs until the first await, sets the pending native continuation (same handler + cookie_for_label_1) on **its** frame, and returns 1. When the evaluator resumes, the top frame is that frame; Phase 1.5 calls the handler with `(ctx, out_ev, 1, resumed_value, cookie_for_label_1)`. The handler branches on the cookie to label 1, consumes the resumed value, continues execution, and either returns 0 (done), or hits another await and sets the continuation on the same frame again and returns 1. If the native body calls into Koskari in between, it sets the continuation on its frame, pushes the Koskari frame(s), returns 0; when the nested call returns, we pop back to the JIT frame and invoke its continuation with the return value. So the **same function** (same `KosCallHandler`) is invoked for both call and every resume; only `is_resume` and the cookie distinguish the phase.

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

When the CALL opcode invokes this callable (via the unified dispatcher), the **kos call handler** for the apply type runs. The handler:

1. Interprets the arguments (1, 2, or 3) from the `args` tuple (and any future kwds convention) to get `fn`, optional `arglist`, optional `kwds`.
2. If `fn` is a type that supports **flattening** (e.g. `KosFunction` or `KosExpression`), the handler does **not** call `fn` via PyObject_Call. Instead it prepares the call namespace from `arglist` and `kwds` (as the normal Koskari call path would) and either:
   - Pushes a new frame for `fn`'s code with that namespace (normal call), or
   - Replaces the current frame with `fn`'s code and namespace (tail call), matching the current opcode (CALL vs TAIL_CALL).
3. If `fn` is not flattenable (e.g. a Python callable), the handler falls back to normal application: build the combined args/kwds and call the default handler (or PyObject_Call + push_or_await) for `fn`.

So from Koskari, `(apply fn arglist kwds)` is equivalent to calling `fn` directly with those arguments when `fn` is a Koskari function or expression—the evaluator flattens into the current context and runs `fn`'s code directly. When `fn` is not flattenable, behavior is the same as from Python (simple application).

### Implementation notes

- The apply callable is a new type (e.g. `KosApplyType`) that has both `tp_call` (for Python) and a registered kos call handler (for the evaluator).
- The handler must map the 1/2/3-argument convention from the CALL opcode's `args` (and any kwds) into the internal (fn, arglist, kwds) form. The compiler will emit CALL with 1, 2, or 3 positional arguments; the handler receives the tuple and interprets element 0 as `fn`, element 1 (if present) as `arglist`, element 2 (if present) as `kwds`. Omitted arguments are treated as no extra args and no keywords (equivalent to `()` and `None` or empty mapping).
- The handler can reuse the same namespace-preparation and frame-push/tail-replace logic as the existing KosFunction and KosExpression handlers, so unification allows apply to "forward" into the same code paths without duplicating them.

**Current apply implementation**: Flattening is used only when `fn` is exactly `KosFunction` or exactly `KosExpression` (e.g. `KosFunction_CheckExact(fn)`), and for expressions only when the positional arglist is empty and `kwds` is NULL. If `kwds` is non-NULL (including an empty dict), the handler does not flatten and uses `PyObject_Call(fn, seq_args, kwds)` then push_or_await, because the flattened call path does not accept keyword arguments. Arglist-to-tuple conversion is implemented in both the kos call handler (eval) and in `KosUtils_Apply` (Python `tp_call`); the two must stay in sync.

---

## Concerns and caveats

The following points are not implied by "planned work" elsewhere and are worth keeping in mind when maintaining or extending the implementation.

- **Flattening and subclasses**: Apply uses `KosFunction_CheckExact` and `KosExpression_CheckExact` to decide whether to flatten. Subtypes of KosFunction or KosExpression do not get the flattened path; they are invoked via `PyObject_Call` + push_or_await. If a future type should be flattenable via apply, either switch to a `Check` (subtype) predicate or add an explicit allowlist.
- **NULL handler fallback**: Any type that passes `KosCallable_Check` but has NULL in the relevant slot (e.g. the base `KosCallableType`, or a subtype that omits the slot) is treated as a normal Python callable: the default handler is used. Document this so that new callable types do not assume "no slot" has a different meaning.
- **Type layout for slot macros**: `KosCallableType_CALL(typ)` and `KosCallableType_TAIL_CALL(typ)` cast `typ` (a `PyTypeObject *`) to `KosCallableTypeObject *`. This is correct only for types defined as `KosCallableTypeObject` (same memory layout). Do not use these macros on other type layouts.
- **Duplicate arglist conversion**: The logic that converts `arglist` (pair, tuple, list, or other sequence) into a tuple lives in both the apply kos call handler (eval.c) and in `KosUtils_Apply` (utils.c). Changes to one (e.g. new sequence types, error messages) should be reflected in the other to avoid behavioral drift.

---

## Summary

- **One path**: All CALL-family opcodes should pop callable and args, then call a single dispatcher. Currently only CALL and TAIL_CALL do so.
- **One contract, same signature for call and resume**: Handlers use a single signature `(ctx, out_ev, is_resume, arg1, arg2)`: initial call has `is_resume==0` (arg1=callable, arg2=args); resume has `is_resume!=0` (arg1=resumed value, arg2=cookie). Return 0 (continue), 1 (suspend), or -1 (error). Continuation can be frame-based (no re-entry) or native (pending continuation re-invokes the same handler with is_resume set). Resume is not yet implemented; all handlers currently error on `is_resume != 0`.
- **Lookup by type**: Handler is resolved via type slot (implemented) or registry (not implemented); default is Python call + push_or_await.
- **Resume**: The pending native continuation (handler + cookie) is stored on a frame, not the context. **The handler implementation decides which frame**: utility callables (apply, map) typically use the **caller's** frame (no frame of their own); JIT or heavy native may push their **own** frame and use it. The evaluator always checks the **top** frame for a continuation (after any revive—await, yield, etc.—or when the top frame changes after a return) and invokes it with (ctx, out_ev, 1, value, cookie). **All suspend types** (await, yield, and any other boundary that delivers a value) use this same mechanism. Storing on the frame gives correct nesting and lifecycle (continuation goes with the frame when it is popped). Native bodies can be re-entered many times across suspends or across nested Koskari calls. **This resume mechanism is not yet implemented.**
- **apply**: First real implementation; from Koskari it flattens when `fn` is exactly KosFunction or KosExpression and (for expressions) arity 0 and no kwds; otherwise behaves like simple application. See **Concerns and caveats** for subclass and arglist-conversion notes.
- **map**: Future example of a meta-opcode-style callable and optional kos next iterator; no evaluator special cases.
