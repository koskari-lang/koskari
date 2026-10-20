# Resumable reads: a deref that calls, and a deref that waits

## Status

Proposal -- not implemented. Builds on the resume kind
([../archive/threaded-control.md](../archive/threaded-control.md)
change 7). Two scope questions are open at the end.

## Summary

A `KosDynamicRef` wraps its get function in `KosDemand_New` at
construction (`ref.c`), so a read of a dynamic ref runs a nested
evaluator to completion on the C stack. That is the demand path, and
it is why a read can never suspend and can never be captured. This
proposal lets the deref answer "call this" instead of calling, so the
row that read the slot installs the getter as an ordinary call: a
raise inside the getter unwinds to the read, an await inside it
suspends as any call's does, a capture across it is a capture across a
call. The same road then carries a ref whose value is not ready yet:
the deref answers with an awaitable, and the row waits on it as a call
whose result needed awaiting.

Raw slots and plain refs are the whole of the hot path, and their
code does not change.

## The rule on performance

The raw arm of `kos_ns_fast_deref` (`namespace.h`) and its inline
`KosRefType` / `KosConstantRefType` arm stay as they are. The only
edit at a row handler is the target of its cold `! val` branch, which
today tails into the raise and under this proposal tails into
`kos_read_failed`: one branch, one tail call, taken only when the read
produced nothing. The name path of a VAR row already resolves through
a ref, and the read entry adds a branch only in the dynamic-ref case.

Two gates per change:

- the opsizing tool ([opcode-sizing.md](opcode-sizing.md)) must show
  no growth in any FAST or VAR row's handler;
- one churn run on binary_trees and fannkuch must show no movement.

A change that moves either is stopped and redesigned.

## The changes

### 1. Row bodies over owned operands

Every row that derefs slots and then does work -- the `FAST_ATTR`,
`FAST_ITEM_FAST`, `CALL_FAST` (and its NAMES, STAR, ATTR forms), FORMAT,
ITER, NEXT, COMPARE, SWITCH, LIST_APPEND and INPLACE families, and
their VAR twins -- splits into the deref prologue it has now and a
`static inline <op>_apply(ctx, insn, a, b, ...)` taking the derefed
operands as separate owned arguments. The handler's fast path derefs
into locals and calls apply, as it does today in one body; nothing
moves through the stack, and the inline compiles to the handler it
replaces. A table `kos_row_finish[op]` of thread functions, one per
family, pops the row's lookups off the operand stack into locals and
calls the same apply; the pure loads (`FAST`, `FAST2` to `FAST4`,
`VAR`, `VAR2` to `VAR4`) have no entry and finish with `RESULT_NEXT`.

The lookup count and field order of every row come from the
`opcodes.incl` schema, where the `VAR` and `FASTVAR` field kinds name
the lookups; a per-op table beside the styles table records them
once. Only `kos_read_failed` and the row kind read that table.

Elected bodies (`eval/eval_electors.c`) are untouched: they classify
on the raw arm and demote to the generic body, and the generic body is
the handler this change splits.

No behaviour changes in this step. It is sequenced first so that
change 2 lands on a tree where every row already has a finish.

### 2. The deref answers "call this"

`KosDynamicRef` stops wrapping its get, set, delete and empty
functions at construction. `KosRef_Deref`, the entry every
non-evaluator caller and the Python-facing `ref.deref()` use, wraps at
call time, and the ref caches the wrapped getter beside the raw one so
that path costs what it does today.

A new `KosRef_Read(ref, &callable)` is the evaluator's entry, with
three answers: a value; NULL with `*callable` set to a new reference
to the getter; NULL with an error set. `kos_ns_fast_deref` and
`frame_ns_deref` take it on the out-of-line arm and return NULL with
no error for "call this".

At every deref site the cold branch `RESULT_RAISE()` becomes
`RESULT_READ_FAILED(ctx, insn, k)`, `k` being the index of the lookup
that failed among the row's lookups, after the handler has pushed the
operands it already holds (the DECREF lists on those branches become
pushes; a push that fails raises). `kos_read_failed(ctx, insn, k)` is
a thread function:

- with an error set, it tails into the raise, as the branch did;
- otherwise it re-reads lookup `k` of the row through the schema table
  (a `FASTVAR` field names a slot, a `VAR` field a name resolved with
  `KosNamespace_FindRef`) for the ref and its getter;
- for a row with one lookup and no finish (`FAST`, `VAR`) it installs
  the getter as a call with no arguments and no resume: the plain
  return arm pushes the callee's value onto the invoker's stack and
  threads at the invoker's saved pc, which the handler had already
  advanced past its row, so the value lands where the load would have
  pushed it;
- for any other row it sets the row kind (change 3) with state `k` on
  the invoking frame and installs the call.

The getter is an ordinary callee frame. Nothing about the call core
changes.

### 3. The row kind

One `KosResumeKind` serves every row:

- **state**: the index of the lookup being resolved, carried in the
  `void *` as an integer. Nothing else survives. The row is
  `code[acting_pc]` on the frame the kind is set on; the install
  flushes `acting_pc` to the frame and the transfer restore carries it
  with the resume pair.
- **the buffer is the operand stack**: resolved values sit where the
  row would have pushed them. A bytecode frame's delivery is always
  the pending slot, never its stack, so the buffered values survive a
  suspension and a freeze untouched.
- **`fn`**: pushes the delivered value, then continues the row from
  `k + 1` over the schema table: a raw or plain slot is pushed in
  place; a slot that answers "call this" sets the same kind with the
  new index and installs; an error raises with the pushed values as
  the frame's operands, which the unwind releases. When the count is
  reached it tails into `kos_row_finish[op]`, or `RESULT_NEXT` for a
  pure load.
- **`clone`**: `kos_resume_clone_shared`. A capture across a suspended
  row is a capture across a call plus one integer.
- **`clear`**: NULL. The stack owns the temporaries.

The delivery arrives settled: with a non-NULL state the delivery step
classifies the value and stages a settling step for an awaitable, so
the resume only ever receives a value.

Safety-net check: the op at `acting_pc` has more than `k` lookups.

### 4. RETURN and YIELD loads

`RETURN_FAST` and `RETURN_VAR` on "call this" install the getter as a
tail call (`KOS_CALL_TAIL`): its return is the frame's return, and no
resume is involved. `YIELD_FAST` and `YIELD_VAR` set a kind of their
own whose `fn` tails into `kos_yield_deliver` with the delivered
value.

### 5. Stores through a setter

`KosRef_Write(ref, value, &callable)` mirrors `KosRef_Read`: stored,
or "call this with the value", or an error. `FAST_STORE`, `VAR_STORE`,
the `_STORE_CONST` rows and the fused store and INPLACE rows take it
on the non-raw arm; on "call this" the row installs the setter with
the value as its one argument under a kind whose `fn` releases the
delivered result and threads on from the saved pc. The store rows'
operands are consumed before the install, so no row kind is needed.

### 6. Reads that wait

`KosRef_Read` gains a fourth answer, an awaitable, from a new ref
kind that holds a future (or any awaitable) and, once it has settled,
the value. The handler's cold branch is the same `RESULT_READ_FAILED`;
`kos_read_failed` sees the awaitable answer, sets the row kind with
`k` (or, for a single-lookup load, a kind whose `fn` pushes and
threads), and tails into `kos_eval_await` with the awaitable. An
awaitable already done delivers on the spot; one that is not returns
the boundary and the Python loop runs the background work; under a
demand driver the boundary forces as it does today. The delivery lands
in the row as a call's does.

What it gives: a binding form (name to be chosen; `background` is the
working name) that stores a future ref, so every read of the name is
a plain FAST until the first read that arrives before the value,
which suspends the context and resumes in the row when the value
lands. A suspended read is a frame with a resume set, so the frame
cache refuses it under the existing rule. The binding form must
produce a non-raw ref, which promotes the slot as a closure capture
does; the template reset returns it to raw at the cache store.

### 7. Rows that collect: UNPACK and the star call

The UNPACK family (`UNPACK`, `UNPACK_FAST`, `UNPACK_VAR`,
`UNPACK_STAR`) and the star-call family (`CALL_STAR` and its VAR, FAST,
ATTR and TAIL forms) materialise an iterable operand in one step,
`PySequence_Fast` in `KosEvalState_PopUnpackSequence` and
`PySequence_Tuple` in `kos_op_call_star`. Over a Koskari generator
that goes through the Python iterator protocol into a `KosPyIter`,
which runs a nested evaluator per element on the demand path: the
same nesting a dynamic ref's getter runs today, forced the same way,
so a generator whose body awaits fails inside the materialisation as a
sync violation where `for-each` over it flattens. Today the demand
path is forced, and that is the accepted behaviour until this change.

The splice and splat rows are the same class: `PAIR_SPLICE` over a
generic iterable segment, `COMBINE_ARGS`, `BUILD_LIST_EXTEND` and
`BUILD_LIST_EXTEND_CONST` materialise through `_PyList_Extend`,
`PySequence_InPlaceConcat` and `PySequence_Tuple`, so `` `(a ,@(gen)) ``
and `#[*: (gen)]` run the generator on the demand path and fail the
same way when its body awaits (probed 2026-10-04). The nested
evaluation runs on the generator's own context, or on a host the
adapter owns, never on the calling context's stack, so the operand
pointers `PAIR_SPLICE` and `COMBINE_ARGS` hold into their segment
across the iteration are not invalidated by it. These rows take the
collecting shape below when it is built.

The row kind carries this without a new node kind. The operand is
one of the row's lookups; when it is a KosIter provider (the
`KosIter_Check` gate `OBJECT_NEXT` already uses) the handler does not
materialise it but collects it:

- **the buffer is the operand stack**: the operands the row already
  holds stay where they are, the provider among them, and each element
  delivered is pushed above them; the row's state `k` counts the
  elements pushed so far.
- **each step is a NEXT node**: the handler, and then the kind's `fn`
  after every delivery, pushes a `KOS_DYN_NEXT` node over the provider
  and runs one `KosIterRequest_Next` against it, exactly as
  `object_next_step` does for `(next it)`. A delivered element lands
  through the pending slot into `fn`, which pushes it and requests the
  next; the node self-pops after each delivery as it does today. A
  request that continues threads into the installed frame or bounces
  through the prologue, and the delivery arrives as a call's does.
- **exhaustion finishes the row**: `fn` gathers the `k` elements off
  the stack into the tuple the materialisation would have produced,
  places it in the provider's position, and tails into
  `kos_row_finish[op]`, which for UNPACK binds the targets and for a
  star call builds the argument tuple and installs the call. The
  operands under the elements are untouched throughout, so a raise in
  the generator unwinds with the frame's operands as they were, and a
  freeze at a yield inside the generator keeps the half-collected
  elements on the stack as any buffered row does.
- **a raise or a capture** mid-collection behaves as for any suspended
  row: the frame carries a resume, the frame cache refuses it, and a
  clone of the row kind is `kos_resume_clone_shared`.

What it gives: `(define-values [a b c] (gen))` and `(f *: (gen))`
flatten for a Koskari generator, awaits inside the generator suspend
the consumer as `for-each` lets them, and a Python iterable keeps the
`PySequence_Fast` path unchanged. The gates of "The rule on
performance" apply: the gate is one type check on an operand the
handler already popped, on the cold arm where the operand is not a
tuple or list.

Two things to settle when it is built:

- whether the star handler's rewrap of a TypeError into "argument
  after `*:` must be an iterable" should stand only for a type with no
  iteration protocol, so that an error raised while iterating, the
  sync violation included, reaches the user as itself;
- `UNPACK_STAR`, which needs the whole sequence before it can split
  the tail, collects to exhaustion like the others; the count check
  moves to the finish.

### 8. Match predicates and extractors: superseded

This section described a resume row for `MATCH_CASE`, the walk's
position moved off the C stack so a predicate or extractor call could
install and the match resume at the test. The engine it would have
driven is gone: a pattern is `MATCH_*` instructions in the frame that
matches, and a `(? pred)` or `(app f ...)` is a `CALL` from that frame
with the pattern's continuation in the frame's own pc
([archive/matcher-bytecode.md](../archive/matcher-bytecode.md)). There is no walk to
resume and no row to add.

## Scope limits

- `KosMemoizedRef` keeps the demand path: its deref writes the value
  back into the ref, which needs a kind of its own, and it is not on
  any measured path.
- `KosAttributeRef`, `KosItemRef` and Python descriptors run Python's
  protocol and stay synchronous.
- `KosParamRef` and `KosWeakRef` hold values and never call.

## Open questions

1. **Scope of the rows.** All rows through the finish table (change 1
   touches about sixty handler files mechanically), or the pure loads
   and the RETURN and YIELD rows only (changes 2 to 4 without the
   finish table), with a fused row answering "call this" by running
   the getter on the demand path as today. The second limits the
   feature to plain reads.
2. **Order.** The refactor first (change 1, then 2 to 6), or the
   feature on plain loads first (2 to 4 on `FAST`, `VAR`, RETURN and
   YIELD) and the refactor and fused rows after. The second shows the
   feature working earlier.

## Testing

- A getter that raises, one that awaits, and one captured across, on
  `FAST` and on `VAR`.
- A `FAST4` with a call-backed slot in every position, and a fused row
  of each family with its lookup call-backed.
- `RETURN_FAST` and `YIELD_FAST` through a getter.
- A store through a setter that awaits.
- `UNPACK` and `CALL_STAR` over a Koskari generator, one whose body
  awaits, one that raises mid-way with the frame's operands intact,
  and one captured across; `UNPACK_STAR` splitting a collected tail.
- A `match` arm whose predicate awaits, one whose extractor awaits,
  one whose predicate raises with the frame's operands intact, one
  captured across, and an arm mixing Python and Koskari predicates;
  a `ONEOF` whose first alternative suspends and then misses.
- Reads that settle immediately; reads that suspend and resume under
  an asyncio driver and under a demand driver; a failed future raising
  at the read site; a suspended row inside a generator and inside a
  delimited region; the frame cache refusing a suspended read.
- The two performance gates on every change.

## Related

- [../archive/threaded-control.md](../archive/threaded-control.md) --
  the resume kind this builds on.
- [../EVALUATOR.md](../EVALUATOR.md) §8.2 -- CPS resumes and the
  delivery step.
- [electors.md](electors.md) -- the elector prologue and the generic
  body.
- `koskari/lib/ref.c` -- `KosDynamicRef` and its demand wrapping.
- `koskari/lib/namespace.h` -- `kos_ns_fast_deref`.
- `koskari/lib/opcodes.incl` -- the row schema.
