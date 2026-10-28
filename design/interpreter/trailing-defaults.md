# Trailing defaults: a steal-bind path for functions with optional parameters

**Status:** open. The user asked (2026-09-27) for a deeper discussion
before any proposal. This note records the cost, the shape a fast path
would take, and the questions the discussion has to settle.

## The cost

A function is "simple" when its formals are exact positionals with no
defaults, star, or keywords: `KosFunction_SIMPLE_ARITY1` holds the
arity plus one, and a call with that many arguments steal-binds the
block straight into a cached frame's namespace
(`KosFormals_SimpleStealApplyBlock`, `kos_dyn_push_ready_frame`).

A function with one trailing default, `[depth x: 1]`, is not simple.
Every call takes the general formals machine: a fresh namespace from
the template, `KosFormals_VectorApplyFast`, and a frame the cache
refuses at return, because every consumer of a cached frame requires
simple arity (brainstorm item 1.6) and the offer admits only what a
consumer could take. The `mc_dflt` microbenchmark measures the
difference at 18 cycles per call over the simple path. The `tree`
churn kernel takes this path on every call; estimated 2-4% there,
about 20% per call for such functions.

## The shape

Classify a third kind: positional formals, defaults only at the tail,
no star and no keyword-only. Record on the function:

- `min_arity` and `max_arity` (the simple case is `min == max`);
- a default vector: the default operands in formal order for slots
  `min .. max-1`.

A call with `min <= nargs <= max` and no keyword names steal-binds the
`nargs` block references into slots `0 .. nargs-1`, fills slots
`nargs .. max-1` from the vector, and continues on the cached-frame path
the simple call already uses. Slots filled from the vector are raw
slots, so `KosNamespace_ResetToTemplate` clears them when the frame is
cached, like any other local. A call with keyword names, or outside the
range, keeps the general path.

The hit test today compares `simple_arity1`, `bind_self_state`, and the
code's yield flag at several sites (`kos_call_simple_install`,
`kos_call_invoke`, `KosFunction_ReadyFrameVector`). A range test replaces the
equality; the "installable arity field" note in
[call-path-followups.md](call-path-followups.md) folds those reads into
one field and belongs with this change.

## What the discussion has to settle

1. **Default modes.** [../language/formal-default-modes.md]
   (../language/formal-default-modes.md) generalises a default from a
   value to share into an operand plus an operation applied at each
   application that omits the argument: `value:` shares, `copy:`
   duplicates, `new:` calls. A vector of values serves `value:` only.
   `copy:` needs a per-call copy of the operand, and `new:` a call,
   which may be a Koskari call and therefore a frame install; the fast
   path either excludes those formals from the classification or fills
   them through a per-slot operation table. Deciding this first decides
   how much of the classification survives.
2. **Self binding.** `bind_self_state` resolves lazily; the self-steal
   arm (`kos_call_invoke_self_steal`) injects the receiver into slot 0
   and shifts the count. The range test has to apply to `nargs + 1`
   there, as the simple test does.
3. **Where the vector lives.** On the KosFunction as a tuple, or as
   the trailing slots of the template namespace already holding the
   default values (the template is copied per call today; a cached
   frame's reset would then have to restore rather than clear those
   slots). The second stores the fact once, but changes what
   `ResetToTemplate` means for a default slot.
4. **Errors.** Arity errors stay on the general path, which already
   reports them; the fast path must only ever take calls the general
   path would accept.

## Measurement plan

Baseline before the first edit on a build proven current: `tree`,
`binary_trees`, and a microbenchmark of a one-default function called
with and without the argument; instruction and cycle counts, plus the
`call_frame_cached` trace counter, which today never increments for
such a function.
