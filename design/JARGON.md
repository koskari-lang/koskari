# Koskari jargon -- reference

The project's admitted jargon: terms that stand in for longer
explanations. Every entry can be unwrapped back to its definition, and
the definition applies consistently across the whole project.

The admission rule: words exist to communicate, and improperly used
jargon is an anti-word. A term earns a place here only when it is
specific, its definition is immediate or consistently applicable
project-wide, and that definition is discoverable (here). Never pick a
new name for something that already has a name -- an iterator is an
iterator. A metaphorical term used in one context needs a lead-in
defining it there, or an entry here. When reporting or documenting,
prefer unwrapping: say what actually happened rather than the label
that compresses it.

Terms are alphabetical. Code identifiers (function, type, enum names)
are not jargon and are not listed; they are looked up in the source.


## ambient async

Koskari execution is asynchronous without syntax: any expression may
suspend on an awaitable, and awaitable results reaching implicit sites
(a call's result, an explicit AWAIT) are automatically awaited
in place. A koskari function called from koskari code produces no
coroutine object; any part of the callee may await, invisibly to the
caller. Futures and Tasks are exempt at implicit sites (they are
handles by convention; only explicit await drives them), as are Defer
objects. Contrast: the demand path.

## boundary

The seam where the python calling convention and the evaluator loop
cross into each other -- in either direction -- where the behavior of
the system being designed can no longer reach. Python calling into
koskari crosses a boundary (tp_call, `__anext__`, the adapters);
koskari calling out to a plain python callable crosses one the other
way. Reserve the word for that seam. (The dynchain's iteration nodes
were once called "boundary nodes"; prefer "iteration node" or the node
kind's name.)

## bootstrap

The core builtins written in Koskari itself (`koskari/bootstrap/*.ki`),
compiled and loaded at package import time. "A bootstrap chain" on a
generator is the not-yet-run frame set created at generator
construction, before its first step.

## chain (dynchain, dyn node)

The unified parent-linked list of dynamic-extent nodes -- frames,
try/finally regions, with and wind extents, loop and foreach regions,
prompts, generator boundaries -- that replaces separate frame and
handler stacks. "On the chain" means installed in the running
context's dynamic extent. Reference: EVALUATOR.md §5, `dynchain.h`.

## codex

The serialization format for compiled `Code` objects (`.kio` files),
written and read by `koskari/serialize/`.

## defer

A single-shot suspended computation object (`KosDefer`): created
without running, driven to completion once when demanded or awaited.
Defers are exempt from ambient auto-await at implicit sites. Reference:
api docs for the defer type.

## demand path (demand, demand-sync)

Synchronous, run-to-completion driving of koskari code: the caller
requires a value now and genuine suspension is an error
(KoskariSyncViolation). Implemented by the `KOS_FRAME_DEMAND` frame
flag, inherited by every frame pushed inside the extent; the denial is
centralized where the evaluator would suspend. An awaitable that
completes without suspending passes. "Demand surface" types (Demand,
DemandGenerator) present this driving to python callers. Contrast:
ambient async.

## durable context

A provider's own `KosContext` that persists across its steps, reported
through the iter suite's `ki_pyctx` slot -- the generator keeps one, so
its suspended frames and context-level state (parameterize extents, an
in-flight await iterator) survive between steps. A provider without one
is driven against an adapter-owned host context instead.

## eager step

Advancing a freshly obtained awaitable by one `send(None)` immediately,
so an awaitable that completes without suspending yields its value with
no suspension machinery. Only a genuine suspension records the
in-flight iterator and suspends the evaluator. The payload that escapes
to the event loop has therefore always been stepped once already.

## extent

A dynamic extent: the span of execution during which a construct is
active, represented by a node on the chain (a with body, a wind, a
parameterize, a loop). "Discardable extents" (NS, LOOP, FOREACH) are
those whose removal is pure reference release.

## flatten (flattened)

Running koskari-defined callables and generators inside the consumer's
own evaluator invocation rather than through python protocol objects: a
generator's frames are spliced onto the consumer's chain and its
yields are delivered in-eval; a mapped koskari function's frame runs as
a child of the requesting step. No intermediary coroutine objects, and
awaits pass through invisibly.

## freeze (frozen)

What a koskari generator does at every yield: its frame chain detaches
from the running context into the generator node's own storage
(`GEN.active`). The next step re-splices (installs) it. Nothing may
assume a yielded generator is still on any chain.

## graft

Transfer-plan vocabulary: splicing a captured chain segment onto
another chain (the PUSH transfer step; `graft_point_id` locates the
splice point for delimited continuation invokes). Reference:
EVALUATOR.md §6.

## host (host context)

The adapter-owned `KosContext` that python-facing adapters (the async
stepper, the DemandGenerator) create to drive a provider that has no
durable context of its own. Discarded and rebuilt after a completion at
its root.

## install

Filling a prepared frame with runnable work. A callable's install slot
(`kc_install` / `kc_vinstall`) fills the frame `do_frame_prep` set up;
a generator install (`kos_dyn_gen_install`) re-splices a frozen chain;
a native program install pushes a code-less frame carrying the
program's descriptor. "Installed work" is work now runnable on the
chain, as opposed to work completed synchronously.

## layout

The indentation-based surface syntax that the reader converts into
s-expression forms (`koskari/reader/layouts/`). A layout block is the
indented body under a form such as `if:`, `try:`, or `define`.

## native program

C code that owns a whole frame on the chain instead of implementing one
opcode: the frame runs no bytecode, and the evaluator invokes the
program's handler in place of instruction dispatch, delivering typed
events (regular invocation, a callee's return, an awaited value, an
offered exception). Described by a static `KosNativeProgram`
descriptor. Current programs: apply's non-flattening call, the map
step. Reference: EVALUATOR.md §8.2.

## park (parked exception)

Exception-stash vocabulary: an exception set aside on the context's
exception stash (`KosContext.exc_stash`) while an asynchronous teardown
runs, tagged with the responsible node's id and re-raised when control
returns. Reference: EVALUATOR.md §4.4. Do not use "park" for iteration
or await suspension; say "suspended" or name the mechanism.

## provider

A KosIter provider: a type whose `kos_iter` suite carries `ki_step`,
making it steppable in-eval by the iteration opcodes and the python
adapters. Current providers: the koskari generator, KosMap.

## special (special form)

A form the compiler handles structurally rather than as a call
(`koskari/compile/specials/`). Contrast macros, which rewrite forms
before compilation.

## suite

One of the inline interface structures embedded in a
`KosEvaluatedTypeObject`: the call suite (`KosCallSlots`, `kc_` slots)
or the iter suite (`KosIterSlots`, `ki_` slots). Membership in a family
is by its defining slot, not by pointer presence.

## transfer plan

The step list a continuation invoke builds and the evaluator drains to
move execution between chains: unwind actions, segment landings, and
the final register load, executed iteratively by the eval loop rather
than recursively. Reference: EVALUATOR.md §6.
