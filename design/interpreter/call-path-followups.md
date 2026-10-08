# Call-path follow-ups: measured items not yet taken

**Status:** a record of findings from the 2026-09 churn investigation
that were verified but not implemented, so the measurements outlive the
scratch files. Each entry names the evidence, the shape of the change,
and what would gate it. The rule learned while closing the inlining
item applies to all of them: fold helpers into the out-of-line call
cores freely, but do not grow the RETURN and CALL handlers or the
preserve-none trampolines with loop bodies; inlining all of the frame
caching into `kos_return_deliver` cost the method kernel instructions
per cycle at equal instruction counts.

## Tail calls to a different function

TAIL_CALL_FAST averages 95 ns against CALL_FAST at 45 plus RETURN at
28. The non-self arm builds a namespace with
`SimpleCallNamespaceFromVector`, increfing every argument, then
`kos_dynframe_reset_for_tail` decrefs them in the block clear. Shape:
steal-bind from the block into the callee's cached frame, cache the
caller's frame, push the callee's ready frame. The self-tail in-place
arm already runs without namespace allocation (`call_tail_inplace` is
zero on the kernels because none self-tail-calls). Helps mutual
recursion and state machines; 121k tail calls across the kernels, so no
kernel will show it.

## Installable arity field

The hit test reads type, `simple_arity1`, `bind_self_state`, `code`,
then the code's flags for yields, repeated in
`kos_call_cached_frame_install`, `kos_call_invoke_impl`, the self path
of `kos_call_invoke`, and `KosFunction_Install`. One field,
`install_arity1`, equal to `simple_arity1` when the code is present and
does not yield and `bind_self_state == 2`, recomputed where
`bind_self_state` resolves. About 3-5 cycles per call. Belongs with the
range test in [trailing-defaults.md](trailing-defaults.md).

## ResetToTemplate word at a time (implemented)

`KosNamespace_ResetToTemplate` runs every time a frame is cached and
on every self-tail. It clears the locals prefix, the only slots a
template holds raw, and copies the template's raw words back; no bit
is tested. It is inlined where the frame is cached (commit a9cd0612),
so its size counts there.

## Frame push and pop bookkeeping

`kos_dyn_push_ready_frame` loads the caller's depth, flags, and live
registers; `kos_eval_check_frame_room` reads the depth again;
`kos_return_is_plain` re-derives "parent is a plain code frame" from
five loads per return. Pass the depth once; a "plain parent" flag set
at push and invalidated where a resume is set. 1-2%; moderate risk
around continuations, which re-parent frames.

## Hot and cold split in handlers

`op_FAST` keeps a `push %rax` / `pop %rax` pair on its hot path because
of calls to `KosStack_PUSH`, `KosRef_Deref`, and
`kos_ns_deref_empty_raw`; the pop alone is 7.4% of `op_FAST`. Elector
bodies set up a frame on the hit path. Move the cold arms into
tail-called cold functions (`op_CONST` is already frameless). In the
push helpers `s->len` is reloaded after the incref (aliasing); a local
holds it. FAST is 10.4G executions, FAST_STORE 3.3G.

## Dispatch prelude

`KOS_THREAD_AT` stores both `es->acting_pc` and `es->pc` on every
dispatch, about 56G stores. Store one and derive the other except where
a handler sets it, or keep a single current-record pointer. Touches
every `SET_PC` site. Related: the dispatch tweaks stub in
[../TODO.md](../TODO.md) under evaluator performance.

## Operand stack indirection

Each push and pop goes `es->block`, then `*block`, a NULL test, then
`len < max`. Option A: compute the maximum stack depth at finalize and
size the frame segment to it, as CPython does, dropping the bounds and
NULL checks. Option B: carry the live segment pointer as a fourth
preserve-none thread argument. Both are larger refactors: extents with
their own segments, and spill and grow, must keep the pointer current.

## Layouts

- **KosDynNode** is 216 B (the 224 B pymalloc class); frame fields sit
  at 40-104 and the inline segment at 128-216, so a call touches four
  or more cache lines. Moving the inline segment after the base header
  is bounded below 1%: L1 load misses were 3.9M against 1.2M on
  binary_trees x10 at about 4 IPC.
- **KosFunction** is 144 B with nine rarely used pointers (doc, dict,
  annotations, signature, three overrides); a lazy side struct brings
  it to about 72 B. Helps per-iteration closure creation.
- **KosPair.position** moves pairs from the 48 B to the 64 B pymalloc
  class; compile-time only.

## Smaller

- **CALL_FAST_ATTR_STAR** goes through `PyObject_GetAttr` and allocates
  bound methods.

## Namespace size survey (investigative)

The user declined namespace size classes (brainstorm 3.11): the grow
realloc is cheap and frame caching may make it moot. Instead: measure
actual function namespace sizes across the opgrams input set, a
disassembly-style survey of slot counts per function, and consider
shrinking `KOS_NS_MIN_SLOTS` slightly if most functions sit well below
it. Nothing changes without the survey.

## Elector quickening through KosOp.fn (open decision)

Each of the 21.3G elector hits pays a prologue hop: dispatch into the
prologue, load the slot, check its kind, then an indirect jump shared by
every site. Elections are stable: 4,986 misses against 21.3G hits. The
proposal has `kos_elector_elect`, the miss demotion, and
`kos_elector_generic_tick` also write or restore `insn->fn`, which today
is written only at finalize; `KOS_TRACE_THREAD_STEP` keys off `op`, so
the counters survive. About 1-2 ns per execution across OBJECT_OPERAND
13.2G, FAST_INPLACE 2.6G, FAST_ATTR_INPLACE 2.4G, CHECK_COMPARE 1.4G.
It conflicts with the sealed instruction stream that
[electors.md](electors.md) requires in its Summary; that conflict is the
decision.
