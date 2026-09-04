# Electors: elected handlers on cache slots and dyn nodes

**Status:** Design proposal -- not implemented.

**See also:** [../archive/attr-lookup-cache.md](../archive/attr-lookup-cache.md)
(the per-site cache framework this extends),
[../archive/threaded-dispatch.md](../archive/threaded-dispatch.md) (the
`KosThreadFn` prototype and musttail rules), [../EVALUATOR.md](../EVALUATOR.md)
§8.2 (CPS resumes), [stencil-compilation.md](stencil-compilation.md) (the
consumer that copies handler bodies per site),
[insn-flags-discard.md](insn-flags-discard.md) (the other finalize-derived
per-record fact), [gen-yieldfrom-passthru.md](gen-yieldfrom-passthru.md)
(the YIELD_FROM follow-up this subsumes).

## Summary

An **elector** is a `KosThreadFn` chosen once, when the fact it depends on
becomes known, and tail-called every time control arrives afterwards. The
opcode handler stops being the place where a type or situation ladder runs
on every execution. It becomes the place that runs the ladder once, records
the winner, and thereafter guards and dispatches.

The instruction stream is untouched. `KosOp.fn` still names the opcode's
handler, the block stays sealable, `.kio` serialization is unchanged, and
`dis` and backtraces see what the compiler emitted. That is the difference
from CPython's adaptive interpreter, which rewrites the opcode in place and
must restore it on deoptimization. Here deoptimization is a pointer store.

Electors have two homes, distinguished by what the elected fact belongs to:

| home | fact belongs to | guard | lifetime |
|---|---|---|---|
| **cache slot** (per site) | an operand the site sees repeatedly | required: the operand can differ next time | the code object |
| **dyn node** (per extent) | the extent itself | none: the fact is fixed at push | the node |

## The elector and the resume

A resume (§8.2) is a `KosThreadFn` that receives one delivery on a frame:
the frame carries `resume_fn` and `resume_state`, the trampoline or a
`RESUME_HOP` invokes it with the settled value, and the pair is cleared.
An elector is a `KosThreadFn` that receives control at a site or a node:
the slot or node carries the pointer, the handler or a delivery invokes it,
and the pointer stays.

Both are chosen at the moment a decision is made and consumed at the moment
control arrives. A resume is chosen when an operation is requested and
consumed when its value lands. An elector is chosen when an operand's shape
or an extent's kind is learned and consumed when the site or node next runs.
They share one prototype, so they compose freely under the musttail rules:

- an elector can end in `RESULT_ENTRY`, arming a resume for the value it
  requested (a call elector on a KosCallable suite);
- a resume can end in a `TAILCALL` to an elector (a send delivery into a
  YIELD_FROM node hops to that node's drive);
- an elector ends in `KOS_THREAD_NEXT` like a handler, so the cooperation
  points are unchanged: `RESUME_HOP`, `RESULT_NEXT_POLL`, frame installs,
  and the prologue still own the breaker test.

The rules that apply to a resume apply to an elector: the `KosThreadFn`
prototype, `KOS_NOINLINE` when it is a direct tail-call target, every owed
`Py_DECREF` before the tail, and no cleanup responsibility for the state it
is handed. A handler that dispatches to an elector is the elector's guard,
and the subsystem that owns the logic exports the elector. The opcode file
holds the guard and the dispatch; the arms of the ladder live where the
ladder lives today (the call core, the iteration request, the yield-from
drive), refactored into `KosThreadFn` entries.

## Cache-style electors

### Layout

A new slot kind in the `KosCacheSlot` union:

```c
KOS_CACHE_ELECTOR,   /* elected handler for a stack- or slot-sourced operand */

struct {
  KosThreadFn fn;          /* elected body, or the generic body when demoted */
  PyTypeObject *type;      /* guard: operand type; borrowed */
  uint64_t type_version;   /* guard for heap types; 0 when the type is static */
  uint8_t misses;          /* consecutive guard misses; drives the demotion */
  uint8_t mode;            /* EMPTY / ELECTED / MEGAMORPHIC */
} elector;
```

The guard is the field cache's guard: a borrowed type pointer, sound because
the operand keeps its type alive during the access and a reused type address
carries a fresh version. Static types (exact `KosFunction`, `PyLong`,
`PyList`) need no version. Heap types whose behavior can change under an
elector (`__call__` reassigned, a `__next__` shadowed) are guarded by the
type version the method cache already reads.

### Handler shape

```c
KOS_OP_HANDLER(CALL_FAST) {
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  PyObject *callable = kos_ns_fast_deref(ns, row.a0);       /* owned */
  if (unlikely(! callable))
    RESULT_RAISE();
  KosCacheSlot *slot = KOS_OP_CACHE_SLOT(es, insn);         /* NULL when ungated */
  if (likely(slot && slot->kind == KOS_CACHE_ELECTOR
             && slot->elector.type == Py_TYPE(callable)))
    TAILCALL(slot->elector.fn, es, insn, callable);
  TAILCALL(kos_call_elect_owned, es, insn, callable);       /* ladder, populate, dispatch */
}

KOS_ELECTOR(call_simple_owned) {                            /* value: the callable, owned */
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));   /* nargs; the window */
  PyObject *const *argv = KosEvalState_StackTailPtr(es, row.a1);
  ...
}
```

The handler fetches the guarded operand once and passes it through as
`value`, ownership transferring with it. The elector's `arg` is the site's
`KosOp`, so the row, the fast slot, the cache index, and the stack window
are one decode away; a site with a secondary operand (the receiver of
`CALL_FAST_ATTR`) passes the guarded one and re-reads the other from the
site. The rule for every elector, in both homes: `value` is the object the
elector was chosen to handle, and everything else is read from the site
through `insn` or from the node through `arg`.

Pass-through is what makes the guard and the body provably act on one
object, and it keeps the refcount round trip that `KOS_CALL_CALLABLE_OWNED`
removed from coming back: a re-deref design would decref before the tail
and incref again inside the elector, and walk the row and the ref cell
twice. Ownership of `value` is a per-family fact fixed in the elector's
definition, the same way each `KOS_CALL_BODY_VARIANT` bakes in its flags
word: a window-sourced elector receives the callable borrowed, a
slot-sourced or resolver-sourced one receives it owned. Electors are the
product of arm and flags word, as the body variants are today.

The electing function has the elector contract and is the ladder as it
exists today, with one addition per arm: on a hit it writes the slot before
dispatching. A guard miss and an ungated site take the same tail call with
the same argument, so populating a slot never changes what a body receives.
Demotion follows the method cache (`eval_cache.c`): a guard miss under an
elected slot increments `misses`, a hit resets it to zero, and at the limit
the slot demotes to `MEGAMORPHIC` with `fn` set to the generic body and the
guard disabled, a bypass that is never re-populated. The limit is a
per-family literal handed to the electing function the way the flags word
is, so one family tunes without touching the others; every family starts
at the method cache's `KOS_METHOD_MISS_LIMIT` of 8. The `KOS_TRACE_OPCACHE`
counters (hit, miss, bypass, uncached) gain an elector row per family.
Deoptimization for any other reason is a write of `kind = KOS_CACHE_EMPTY`.

Under `KOS_EVAL_SAFETYNET` the handler re-runs the ladder on a guard hit and
asserts it names the elected body before dispatching. Assert only; the
dispatch is unchanged.

An ungated site (`compiler/cache-expression` off, or `KOS_CACHE_UNSET`)
tail-calls the electing function every time, which is today's ladder plus
one branch.

### Where it pays: survey

**CALL family** (`CALL`, `CALL_NAMES`, `CALL_VAR*`, `CALL_FAST*`, the
`TAIL_CALL*` twins). The flags word already folds per handler and the
`KosFunction` classification (`SIMPLE_ARITY1`, `BIND_SELF_STATE`) already
hoists onto the callable, so the ladder for the dominant target is short.
The win is the return path: the handler calls a `KOS_NOINLINE` body, which
materializes a `KosOpResult`, which `RESULT_RELAY` tests before threading
on. An elector threads on itself. Electors, one per arm of
`kos_call_invoke_impl`:

- exact non-yielding `KosFunction`, simple arity, steal-bind, with the
  parked-frame hit as its first branch;
- `KosFunction` generator function (agen construction, the window drops
  first);
- plain Python vectorcall with a non-awaitable result, which today walks
  TAIL, KosFunction, `KosCallable_CheckSlots`, `KosInstanceCall_Check`,
  and the awaitable gate before pushing;
- `KosCallable` suite install;
- instance `__call__` injection;
- the general formals machine (kwnames, defaults, star), as the fallback
  arm rather than a demotion.

The `*_ATTR*` members already hold a `KOS_CACHE_METHOD` slot. They request
a run of two, and the elector's guard is on the resolved callable, not the
receiver. The `*_STAR` members are cold and stay on the ladder.

**OBJECT_NEXT, VAR_NEXT, FAST_NEXT**. `object_next_step` branches on
`KosIter_Check` (provider node push and request) versus
`op_get_next_from_iter` (sync `tp_iternext`, async `__anext__`) per step.
Electors: KosIter provider, exact sync iterator, Python async iterator.
The region form (`ITER_NEXT`) is node-style below.

**AWAIT**. `push_or_await_plain` is a type-level gate followed by a Defer
probe, a future probe, and the eager step. Electors keyed on the awaited
type: `KosCoroutine`, `KosDefer`, `PyCoro`, generic `am_await`. Moderate;
the gate is already two loads.

**OBJECT_OPERAND, OBJECT_INPLACE, FAST_INPLACE(_CONST), VAR_INPLACE(_CONST)**.
The hottest opcode by dispatch count. `kos_operand_reduce` selects the
operator from the arg, then each operator tries its `PyLong` two-operand
fast path before the `PyNumber` slot. An elector keyed on (operator, left
type, right type): exact `PyLong` pair, exact `PyFloat` pair, exact
`PyUnicode` concatenation, generic slot. The operator is a site constant,
so the elector table is indexed by it and the guard is on the two operand
types. Two guards, both borrowed compares.

**OBJECT_COMPARE(_VAR/_FAST), CHECK_COMPARE(_VAR/_FAST), SWITCH_EQ\***.
`kos_compare_chain` takes the selector and walks the operands; the extended
tail is out of line. Electors: exact `PyLong` pair, exact `PyUnicode` pair,
identity selectors, generic rich compare. The `CHECK_*` forms fuse the jump,
so the elector owns the branch too.

**OBJECT_FORMAT(_CONST), FAST_FORMAT(_CONST), VAR_FORMAT(_CONST)**. The
fused format opcodes test `PyUnicode_CheckExact` and the spec shape per
call. Electors: exact str with empty spec, exact int, generic `__format__`.

**ITEM, ITEM_STORE, CONST_ITEM(_STORE)**. Subscript through
`PyObject_GetItem`. Electors: exact `PyDict` with hashable const key, exact
`PyList` with `PyLong` index, exact `PyTuple`, generic mapping. The
`CONST_ITEM` forms have the key as a site constant, so the dict elector can
pre-hash it into the slot.

**OBJECT_ITER, VAR_ITER, FAST_ITER, PUSH_FOREACH's acquisition**.
`kos_iter_acquire` chooses sync versus async acquisition. An elector here
is small on its own; its value is that it can pre-elect the step elector on
the FOREACH node it is about to push (below).

**UNPACK, UNPACK_STAR**. `PySequence_Fast` on every execution. Electors:
exact tuple, exact list, generic. Low.

**VAR, VAR_STORE, VAR_INPLACE**. `KosNamespace_FindRef` walks the parent
chain per lookup. A "found at depth d" elector needs a guard that the
namespace framework does not provide (no dict version on a `KosNamespace`,
and the chain shape varies per activation). Investigative only; hot code
uses `FAST`.

Not worth a slot: `CAR`, `CDR`, `CONS`, `NILP` (one inline check),
`PAIR_SPLICE`, `COMBINE_ARGS`, `BUILD_LIST_EXTEND` (cold ladders),
`MATCH_CASE` (already a guard-free cache).

## Node-style electors and resumes

### Layout

A dyn node arm that carries an elected fact gets `KosThreadFn` fields set
at push time by the code that classifies the extent:

```c
/* KOS_DYN_YIELD_FROM arm */
KosThreadFn drive;       /* one of four, by delegate kind; set at push */

/* iteration request record (FOREACH region, NEXT node) */
KosThreadFn step;        /* provider ki_step, sync tp_iternext, async anext */

/* KOS_DYN_FRAME arm */
KosThreadFn on_return;   /* plain / region / resume parent; set at install */
```

No guard. The delegate of a YIELD_FROM node, the iterator bound to a FOREACH
request, and the parent shape of an installed frame do not change for the
life of the node. A multi-shot continuation clone copies the node and the
pointer with it; the pointer is to a static function, so the clone is
correct by copying.

### Resumes on nodes

Several opcodes are re-entered on each delivery by rolling the pc back to
the acting instruction (`YIELD_FROM`, `POP_WITH` on an async exit, `PUSH_WITH`
on an async enter, `YIELD`, the prompt/shift pops) and re-deriving the phase
from node state. Under §8.2 a delivery already has a route that does not
touch the instruction stream: the frame's `resume_fn` / `resume_state`
pair. A node-style resume arms that pair with a phase function and the node
as its state. The opcode then runs once per node, and every later delivery
lands on the phase function, which reads its node, does the phase's work,
and either arms the next phase or threads on past the opcode.

That distinguishes two kinds of resume state, which the capture rule must
tell apart. A step resume's state (map, filter) is owned by the step object
and cannot be replayed, so `kos_dynframe_clone` refuses by name. A node
resume's state is the node, which the clone already copies, so a frame
carrying one is capturable. The frame's `resume_name` is the natural
discriminator, or a flag beside it.

### Where it pays: survey

**YIELD_FROM**. The strongest case. Two axes are folded into one 766-line
handler. The kind axis (`KOS_YF_SYNC_ITER`, `SYNC_GEN`, `PY_ASYNC_ITER` /
`PY_ASYNC_GEN`, `KOS_ITER`) is fixed at push and re-decoded on every
re-entry at the ladder near the handler's end; it becomes `drive`, elected
in `yield_from_normalize`. The phase axis (`EMIT`, inject-throw,
`EXPECT_SEND`, `EXPECT_AWAIT`, `DRIVING`) is re-derived from node state on
every re-entry; each phase becomes a resume armed on the frame with the
node as state. The handler shrinks to: pop the subexpression, normalize,
push the node, elect `drive`, tail-call it. The four drive bodies and the
phase resumes are shared out-of-line functions.

**FOREACH region and NEXT node**. `KosIterRequest_Next` branches on
`islots` versus `op_get_next_from_iter` on every `ITER_NEXT`, and the
request record's mode (`INSTALLED`, `GRAFT_VALUE`, `AWAITING_PY`) is a
phase machine consulted at revive. `step` is elected when
`kos_dyn_iter_request_bind_iter` binds the iterator; the mode transitions
become the resume armed for the delivery. `ITER_NEXT` becomes: find the
region, arm the else pc, tail-call `step`.

**FRAME return**. `RETURN` classifies its parent on every execution:
`kos_return_is_plain`, `kos_return_is_region`, `kos_return_is_resume`, each
a chain read, then the general walk. The call core knows the parent shape
at install. `on_return` is elected there, and `RETURN` reduces to one check
(no extents above the returning frame, which is `dyn_top == frame`) and a
tail call. Extents the callee pushed and failed to pop are the walk's case
as today. This is on every call's hot path.

**WITH and DYN_WITH**. `resolve_context_enter_exit` already runs once per
node and stores enter, exit, and the self-bind word on it. What is
re-derived per delivery is the phase: `PUSH_WITH` handles the async-enter
revive and the KosCallable-enter install as two distinct continuations
through `kos_dynnode_on_control_returned`; `POP_WITH` rolls the pc back on
an async exit and consults `EXIT_PENDING` on a KosCallable exit. Those are
two node resumes (enter-settled, exit-settled) armed at wind and unwind.

**GEN node**. A send or throw into a suspended generator is classified by
the prologue's inject step (`py_resume_inject`) at re-entry, before the
instruction after `YIELD` runs. The generator's receive is a node resume
by construction: the inject step becomes the phase function armed when
the generator suspends.

**WIND**. `PUSH_WIND` / `POP_WIND` call the before and after thunks and
await their results; each is a request with a resume.

**PROMPT, SHIFT, CC**. These nodes already carry a `resume_pc` and, for the
in-place delimited invoke, a displaced stack. A `resume_fn` beside the pc
is the same idea expressed the same way as every other node. Investigative:
the transfer-plan machinery owns these paths and the gain is uniformity
rather than speed.

## Tracing

`KOS_TRACE_THREAD_STEP` records `insn->op` at the fetch. With electors, the
op no longer says which body ran. Each elector is declared through a macro
that defines the function and registers it in a table generated by
`tools/opimpl.py`, the way `handlers.incl` is generated for opcodes; the
elector's index in that table is its id. Under a tracing build the elector
records its id on entry, so the per-opcode timing planned at the fetch
buckets by (op, elector). The slot stores the bare `fn`; the id is
recovered from the table when tracing is on and costs nothing otherwise.

`Code.cache_slots` introspection gains the elector kind, reporting the
elected function's name and guard type per site.

## Stencil consequence

A stencil copies a handler body per instruction. After this split, the body
of `YIELD_FROM`, `CALL_FAST`, `RETURN`, and `ITER_NEXT` is the guard and the
dispatch, a few dozen instructions each. The elected bodies and the phase
resumes are shared functions the stencil reaches by an indirect tail call
through the slot or node, never copied. The patch site for an elector
dispatch is the slot address, which finalize already knows.

## Where to begin

Ranked by expected benefit against risk:

1. **YIELD_FROM, node-style.** Largest structural change, no guard
   machinery, and it retires the pc-rollback re-entry pattern that the
   other node-style candidates share. It also settles the capturable
   node-resume rule.
2. **CALL_FAST and CALL, cache-style.** The hottest cache-style site,
   with the arms already separated in the call core. Two electors first
   (exact simple `KosFunction`, plain vectorcall), measured with the churn
   bench, before the rest of the family.
3. **FRAME `on_return`, node-style.** Small change, every call pays for
   it today, and it pairs with item 2 (the install elects the return).
4. **FOREACH `step`, node-style.** Follows the YIELD_FROM shape on a
   simpler node.
5. **OBJECT_OPERAND and OBJECT_COMPARE, cache-style.** The type-pair
   guard is the new piece; the arms exist as the `long_fast_*` functions.

## Sizing

Neither union grows. The elector arm is smaller than the method arm of
`KosCacheSlot`, and the `with` arm is the largest of `KosDynNode`, so a
pointer on the `frame`, `yield_from`, and `foreach` arms fits; a `sizeof`
check at implementation confirms it. Should a kind ever outgrow the slot,
the assembler already reserves a run per site: the kind requests two slots
in `opcodes.incl` and its layout spans the pair.

# The end.
