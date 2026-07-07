# `values` as a KosCallable install target

Promote `KosValuesType` from a plain `PyTypeObject` to a
`KosCallableTypeObject` so a `values` object invoked from Koskari bytecode is
spliced directly into the evaluator (a *flattened* call) instead of trampolining
out through generic `tp_call`. The behavior is the mirror image of `KosPartial`:
where a partial captures the callable and defers the trailing arguments, a
`values` captures the arguments and defers the callable.

Files: `koskari/lib/values.c` / `values.h`, `koskari/lib/callable.h` (interface),
`koskari/lib/eval/eval_utils.c` (`do_frame_prep`, `KosEvalState_Call*`),
modelled on `koskari/lib/partial.c` and `koskari/lib/func.c`.

## Model: the inverse partial

A `KosValues` stores `args` + `kwds` and nothing else (`KosValues_ARGS`,
`KosValues_KWDS`). It is called with **at least one positional, the callable to
apply**, followed by optional extra positionals and keywords. On invocation it
merges its stored arguments with the invocation arguments and applies the
callable:

```
(vals fn extra... :k v...)
  target    = fn
  positional = vals.args ++ (extra...)      ; stored first, then invocation
  keyword    = merge(vals.kwds, {k: v...})  ; invocation keywords win on clash
```

This ordering is exactly what `values_call` produces today
([values.c:143](../../koskari/lib/values.c)); the install slots MUST share that
merge core so the Koskari-bytecode fast path and the Python-visible `tp_call`
never diverge. `tp_call` stays installed (as `func.c` keeps `function_call`
alongside its install slots) for calls that originate from pure Python.

The empty-invocation guard is preserved: a `values` called with zero positionals
raises `TypeError` ("values objects must be called with at least one argument,
the function to apply").

## The wrinkle: late target binding

`KosPartial` knows its inner callable at construction, so its
`tp_kos_wants_frame_fn` can inspect the inner and answer the frame-prep question
honestly *before* the install runs. `do_frame_prep`
([eval_utils.c:351](../../koskari/lib/eval/eval_utils.c)) then prepares exactly
the frame the inner will fill, and `kospartial_kos_install` calls the **bare**
`KosCallable_CallArgs` on an already-correct frame
([partial.c:203](../../koskari/lib/partial.c)).

`values` cannot do this. Its target is the **first positional of the incoming
call**, which `wants_frame_fn(callable_instance)` never receives. So a `values`
cannot participate in the pre-install frame negotiation; the negotiation must
move **into the install slot**, where the target is finally known. This is the
central design decision below.

## Decision: fix `wants_frame = 1`, reconcile at install

Declare:

```c
.tp_kos_wants_frame    = 1,
.tp_kos_wants_frame_fn = NULL,   /* cannot inspect the not-yet-known target */
```

Rationale: `values` must pick a fixed answer without seeing the target.
Over-provisioning a frame is safely reversible — dropping a surplus frame is
well supported (`kos_dyn_pop_frame`, and the tail path's pop-if-`dyn_top`).
Under-provisioning is not: in the tail case `do_frame_prep(W=0, tail)` pops the
`values` frame, and a later frame-wanting target would `reset_for_tail` the
**parent** frame and clobber it. Choosing `W=1` puts the important case
(tail-call into a frame-wanting target) on the path that needs no fixup at all,
and confines every fixup to the harmless drop-a-frame direction.

With `W=1`, `do_frame_prep` leaves the install slot one of two frame states:

- **tail:** `kos_dynframe_reset_for_tail(frame)` — the `values` frame is reused
  as a fresh, empty tail frame.
- **non-tail:** `kos_dyn_push_frame_empty` — a new empty frame is pushed.

The install slot then classifies the target and reconciles:

```
t_flat = KosCallable_Check(target)
         && effective_wants_frame(target)   /* per-instance hook or static flag,
                                                same helper shape as
                                                kospartial_inner_is_flattenable */
```

### Case A — `t_flat` (target is a frame-wanting KosCallable)

The frame `do_frame_prep` prepared is exactly what the target's install wants
(reset-for-tail when tail, freshly pushed otherwise). Dispatch through the
**bare** entry, forwarding `tail_call` unchanged:

```c
KosCallable_CallArgs(es, target, merged_args, merged_kw, tail_call);
```

No second frame prep runs, so there is no parent-clobber risk. **TCO is
preserved:** in tail position the target fills the reused frame with no stack
growth. This is the case `W=1` was chosen to make free.

### Case B — not `t_flat` (plain Python, or a KosCallable that wants no frame)

The prepared frame is surplus for a `W=0`-style target. Undo it, then re-dispatch
through the **full-service** entry `KosEvalState_CallArgs`, which re-checks the
target and applies its own (`wants=0`) prep — safe because a `wants=0` target
never resets or pushes a frame that could clobber the parent:

- **non-tail:** `kos_dyn_pop_frame(es)` first to drop the pushed empty frame,
  *then* `KosEvalState_CallArgs(es, target, merged_args, merged_kw, 0)`.
  Without the pre-pop, a plain-Python target's result would be pushed onto the
  surplus frame and that frame would never be popped.
- **tail:** do **not** pre-pop. `KosEvalState_CallArgs(es, target, merged_args,
  merged_kw, 1)` runs `tail_prep_py_call`, which pops the reused frame when it is
  `dyn_top` and delivers via `RETURN` (plain Python) or dispatches a `wants=0`
  KosCallable — both already correct.

### Reconciliation matrix

| target | tail? | frame after `do_frame_prep` (W=1) | fixup | dispatch |
|---|---|---|---|---|
| frame-wanting KosCallable | yes | frame reset-for-tail | none | bare `KosCallable_CallArgs(tail=1)` |
| frame-wanting KosCallable | no | empty frame pushed | none | bare `KosCallable_CallArgs(tail=0)` |
| plain Python / no-frame KosCallable | yes | frame reset-for-tail | none | full `KosEvalState_CallArgs(tail=1)` |
| plain Python / no-frame KosCallable | no | empty frame pushed | `kos_dyn_pop_frame` | full `KosEvalState_CallArgs(tail=0)` |

The only asymmetry is the single pre-pop in the bottom row.

## vinstall (vectorcall)

Provide `tp_kos_vinstall` as well as `tp_kos_install`, modelled on
`kos_vinstall_function` ([func.c:1840](../../koskari/lib/func.c)). The reconcile
logic (`t_flat` classification, frame fixup, bare-vs-full dispatch) is identical;
only the argument assembly differs — the vector form splits `args` / `nargs` /
`kwnames` rather than a tuple + dict. Both slots delegate to one shared
merge-and-reconcile core so the two entry points stay in lockstep with each other
and with `values_call`. The target is `args[0]`; the merged positionals are
`vals.args ++ args[1:]` and merged keywords fold `kwnames`/stored kwds with the
same invocation-wins rule.

## Type promotion mechanics

- Change `KosValuesType`'s definition to a `KosCallableTypeObject`, wrapping the
  existing slot table under `.type = { … }` and setting
  `.tp_base = KOS_CALLABLE_PYTYPE(KosCallableType)`. Preserve every existing slot
  (`tp_as_number`, `tp_as_sequence`, `tp_as_mapping`, `tp_iter`, `tp_hash`,
  `tp_richcompare`, GC slots, `tp_call`, `tp_repr`).
- Add `.tp_kos_install`, `.tp_kos_vinstall`, `.tp_kos_wants_frame = 1`,
  `.tp_kos_wants_frame_fn = NULL`.
- Route `PyType_Ready` through the base (as `KosPartialCallableType` /
  `KosFunctionType` do) so `KosCallable_Check` recognizes `values`.
- No `tp_dictoffset` is required unless dunder support is later wanted; the
  install path needs none.

## Tests

- `values` invoked from Koskari bytecode dispatches without a `tp_call`
  trampoline (assert flattened path taken).
- Target permutations: KosFunction (frame-wanting KosCallable), a bound method,
  a plain Python callable, and a builtin — each in both tail and non-tail
  position.
- Argument merge: stored-only, invocation-only, both; keyword clash resolves to
  the invocation value; stored-args-precede-invocation ordering.
- Empty-invocation `TypeError` preserved on the bytecode path, matching
  `values_call`.
- Tail-position values-call into a self-recursive KosFunction does not grow the
  frame stack (TCO regression guard for Case A).
- Non-tail Case B leaves no dangling frame (drive a loop of values-calls to a
  plain-Python target; assert `dyn_top` depth is stable).
- Parity: the same call via `tp_call` (pure-Python entry) and via the install
  slot produce identical results and side effects.

## Ref

[partial.c](../../koskari/lib/partial.c) (mirror pattern),
[callable.h](../../koskari/lib/callable.h) (interface),
[special-call-protocol.md](special-call-protocol.md),
[OBJECT-MODEL.md](OBJECT-MODEL.md),
[../interpreter/eval-state-live-authority.md](../interpreter/eval-state-live-authority.md),
[../interpreter/evaluator.md](../interpreter/evaluator.md).
