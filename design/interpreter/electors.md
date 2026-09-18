# Electors: elected handlers on cache slots and dyn nodes

**Status:** The node-style home is built on YIELD_FROM (commits
d5b29ddc, f4719b9a, b9a116a0): the drive elected onto the node at push,
one phase resume per suspension armed on the delegating frame, and the
handler running once per delegation with the pc never rolled back. On
the yieldfrom kernel over five samples that is 19.8% fewer cycles and
42% fewer branch misses for 3.6% more instructions. The delivery
contract it settled is recorded in [../EVALUATOR.md](../EVALUATOR.md)
§8.2. The cache-style home is built on the arithmetic families:
OBJECT_OPERAND, OBJECT_INPLACE, FAST_INPLACE, and FAST_INPLACE_CONST
each own a `KOS_CACHE_ELECTOR` slot and a prologue handler, with one
int body per operator elected on the site's first exact-int operands
(`eval/eval_electors.c`). Measured on the churn kernels over the inline
two-digit int read, the OBJECT_OPERAND elector ran 15.0% fewer
instructions and 11.8% fewer cycles on `branch`, 13.4% and 14.2% on
`loop`; against a control that hoisted the int pair ahead of the
operator switch with no slot it ran 1.8% and 1.5% fewer instructions
and 5.8% fewer `branch` cycles. The in-place families added 4.0% and
4.1% on `fannkuch`, 5.1% and 3.2% on `loop`, with `branch` and
`binary_trees` level. The same home was first tried on CALL_FAST
(commit b73d9499) and removed as flat: 0.6% fewer instructions at the
same cycles, because a call site whose ladder is one type compare and
three loads has nothing for an elector to remove. The elector pays
where the ladder it replaces is long: the operand path was a noinline
call, a switch, a count check, two type checks, and two out-of-line
conversions per execution. One finding from the in-place step: once
four families used the `long_fast_*` paths the compiler stopped
inlining them, costing 3.9% of `branch` instructions until they were
marked `KOS_OP_INLINE`; a body macro that grows a family should be
measured for that. The compare family followed: CHECK_COMPARE and
OBJECT_COMPARE each own a slot and elect one int body, the selector
being a site constant the mask compare reads. That is 3.1% fewer
instructions on `branch`, whose branch conditions are computed pairs,
level on `fannkuch` and `binary_trees`, and 1.4% more on `loop` for a
cause the code `loop` runs does not show: every function on its path
is byte-identical between the two builds and a profile takes none of
the changed cold paths. CHECK_COMPARE_FAST was tried with a slot and
dropped: its handler already runs its int arm inline, so the slot
only added the hop, 1.5% of `loop` instructions and 5.9% of `fannkuch`
cycles. Two more findings from that step: the per-opcode generic-body
lookup, inlined into every int body's decline tails, had grown each
body by a fifth and cost cycles across the kernels until it was moved
out of line; and the compare family's footprint on the churn kernels
is small (370M CHECK_COMPARE executions against 12.3G for
OBJECT_OPERAND on the trace), so its value is general rather than
kernel-shaped. Two steps followed on the same bodies. A result is
written into the operand the stack alone owns: when the left operand
or the accumulator is an exact int with a refcount of one, the result
lies outside the small-int range so the cached identities stay
CPython's, and it needs no more digits than the object holds, the
digits and tag are rewritten in place and no allocation or release
runs; on `loop` that is 25.8% fewer instructions and 24.1% fewer
cycles, on `branch` 9.2% fewer of both, and under `tox -e churn`
`loop` runs at 0.79x of CPython and `tree` at 0.86x. Float bodies then
joined the int bodies on every family, six operators each (add,
subtract, multiply, true division, and ports of `float_rem` and
`float_divmod`; power stays generic for its complex and zero-base
cases), with the same in-place result rule and a float body on each
compare family settling on the C comparisons so NaN answers as Python
does; `nbody` runs 24.0% fewer instructions and 24.2% fewer cycles,
1.91x of CPython under churn from 2.31x. The suite under ASan found
that the elector tests shifted by 2^40, a 128 GiB int that raises
MemoryError on a normal build and trips the sanitizer's allocator
limit; the sanitizer gate belongs in every step that writes into live
objects. FAST_ATTR_INPLACE followed as the first site with two slots:
the field cache stays the first, untouched in layout, and the elector
is the second. Its bodies re-check the field guard on every execution
and take the mode and index from the field slot, load the accumulator
borrowed from the slots member or the inline value, compute with it
as the in-place candidate, and store through the same location only
when a new object came back; a site elects on its second execution,
once the generic body's first run has resolved the field slot. On
the churn kernels against the merge baseline that is 47.7% fewer
instructions and 43.2% fewer cycles on `field`, 46.8% and 44.8% on
`field_slots`, 11.6% and 14.7% on `method`. Str bodies followed on
every family for `+`: the left operand or the accumulator is taken
from its home (the stack cell, the raw fast slot, the `__slots__`
member) and appended through `PyUnicode_Append`, which grows it in
place under CPython's own rule when that home held the only
reference, so `incr!` on a str local grows it as `s += x` does in
CPython; a stack site of three or more strs elects a join that
allocates once at the widest kind. `build_str` became the native
builder behind both OBJECT_STR and the runtime `#str`, replacing a
Python lambda; measured against the join it was a wash, so it keeps
its tuple and `str.join` for mixed pieces. A `strcat` churn kernel
(a count-three `+` appended to an accumulator) runs at 1.23x of
CPython. A review of that step found the one defect the electors
have shipped so far: the slot bodies offered the peeked accumulator
as the in-place home whenever its refcount was one, and the peek
reads through a constant ref as well as a raw slot, so `incr!` on a
`bind! x (constant ...)` name rewrote the constant's digits and never
reached the store that raises; a result is now written in place only
when the slot is raw. The same review dropped the attribute bodies'
unreachable setattr arm (the head now takes the location's address
and the store swaps the result into it), collapsed the int and float
twins of every body into one macro per family parameterised on an
operand kind, with byte-identical code, and added fold bodies:
OBJECT_OPERAND and OBJECT_INPLACE sites of three or more operands of
one kind fold the run in C, decline to the generic body before
anything is materialised, and materialise once, into the first
operand when the stack alone owns it; the reduce fold behind the
runtime operators offers its accumulator for reuse the same way.
The fold covers the operators the compiler emits runs for, since a
third operand to power is a modulus and remainder and the shifts are
binary forms. On `nbody`, whose inner loop has one count-three `+`,
that is 2.4% fewer instructions. A second review caught a regression
in the reduce fold (a protocol slot returning its left operand, as a
list's `__iadd__` does, returns a new reference, which the reuse
test had taken for an in-place write) and the fold reproducer
surfaced a module-packer defect older than the branch: the segment
plan summed members at pointer alignment while the arena reserves
each on a 32-byte boundary, so a module of many small functions could
overflow its own arena; the unit is now `Arena.UNIT` and the plan
aligns to it.

**See also:** [../archive/attr-lookup-cache.md](../archive/attr-lookup-cache.md)
(the per-site cache framework this extends),
[../archive/threaded-dispatch.md](../archive/threaded-dispatch.md) (the
`KosThreadFn` prototype and musttail rules), [../EVALUATOR.md](../EVALUATOR.md)
§8.2 (CPS resumes), [stencil-compilation.md](stencil-compilation.md) (the
consumer that copies handler bodies per site),
[../archive/insn-flags-discard.md](../archive/insn-flags-discard.md) (superseded), [../archive/gen-yieldfrom-passthru.md](../archive/gen-yieldfrom-passthru.md)
(the YIELD_FROM follow-up this subsumes, archived).

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
the frame carries `resume_kind` and `resume_state`, the prologue or a
`RESUME_HOP` invokes the kind's `fn` with the settled value, and the pair
is cleared.
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

The slot kind in the `KosCacheSlot` union, as built:

```c
KOS_CACHE_ELECTOR,   /* handler elected for the site */

struct {
  KosThreadFn fn;          /* elected body; the generic body once demoted */
  uint8_t misses;          /* guard misses since the election */
  uint8_t mode;            /* ELECTED / MEGAMORPHIC */
} elector;
```

The slot holds no guard: each elected body carries its own, so the
handler's hot path is the slot load and the tail call with no compare.
The arithmetic bodies guard with `PyLong_CheckExact` on the peeked
operands, a static type that needs no version. A family electing on a
heap type whose behavior can change (`__call__` reassigned, a
`__next__` shadowed) would guard in its body with the type version the
method cache already reads.

### Handler shape

```c
KOS_OP_HANDLER(OBJECT_OPERAND) {
  KosCacheSlot *slot = KosOp_SLOT(insn);                    /* NULL when ungated */
  if (likely(slot && slot->kind == KOS_CACHE_ELECTOR))
    TAILCALL(slot->u.elector.fn, es, insn, NULL);
  TAILCALL(el_operand_elect, es, insn, NULL);               /* classify, settle, dispatch */
}

KOS_ELECTOR(operand_int_add) {                              /* one per operator, from a macro */
  PyObject *const *args = kos_stack_tail_ptr_fast(*bl, 2);  /* the pair, peeked */
  if (! (PyLong_CheckExact(args[0]) && PyLong_CheckExact(args[1])))
    TAILCALL(el_operand_miss, es, insn, value);             /* count; generic this once */
  rc = long_fast_add(args[0], args[1], &out);
  if (rc <= 0)                                              /* declined: generic, no count */
    ...
  kos_elector_replace2(bl, args, out);                      /* result over the left, drop the right */
  RESULT_NEXT(es);
}
```

The built families read their operands from the site: the stack pair
through the block, the accumulator through `kos_ns_fast_peek`, the
constant off the row. Their `value` is NULL. The elector's `arg` is the
site's `KosOp`, so the row, the fast slot, the cache slot, and the
stack window are one load off the record. A family whose guarded
operand is owned by the handler (a call's callable) passes it through
as `value`, ownership transferring with it; a site with a secondary
operand (the receiver of `CALL_FAST_ATTR`) passes the guarded one and
re-reads the other from the site. The rule for every elector, in both
homes: `value` is the object the elector was chosen to handle, and
everything else is read from the site through `insn` or from the node
through `arg`.

Pass-through is what makes the guard and the body provably act on one
object, and it keeps the refcount round trip that `KOS_CALL_CALLABLE_OWNED`
removed from coming back: a re-deref design would decref before the tail
and incref again inside the elector, and walk the row and the ref cell
twice. Ownership of `value` is a per-family fact fixed in the elector's
definition, the same way each `KOS_CALL_BODY_VARIANT` bakes in its flags
word: a window-sourced elector receives the callable borrowed, a
slot-sourced or resolver-sourced one receives it owned. Electors are the
product of arm and flags word, as the body variants are today.

The electing function has the elector contract and runs the family's
classification once: an exact-int pair at a binary site (the peeked
slot value and the operand, for the fast-slot families) elects the
operator's int body; anything else elects the generic body, which is
the former handler out of line. An empty slot and an ungated site
take the same tail call, so populating a slot never changes what a
body receives. A body's guard miss tail-calls the shared miss
function, which counts and runs the site's generic body for that
execution; an int the fast path declines (wider than a long long, an
overflowing result, a negative shift count) takes the generic body
without counting, since the site is still an int site. Misses count
since the election; the hit path takes no store. At
`KOS_ELECTOR_MISS_LIMIT` (8, the method cache's) the slot demotes to
`MEGAMORPHIC` with `fn` set to the generic body. The generic body is
the slow path, so it can afford a count of its own: it ticks a byte of
its own on every execution, apart from the misses that drive the
demotion, and empties the slot when the byte wraps, so a demoted site,
or one whose first operands were not its steady type, classifies again
every 256 executions rather than never. A decline from an elected body
(an index past a list's end, a key a dict lacks) runs the generic body
and ticks; it is not a guard miss. The
`KOS_TRACE_OPCACHE` counters `elector_hit`,
`elector_miss` (an election or a guard miss), `elector_bypass` (a
demotion), and `elector_uncached` are shared by the families.
Deoptimization for any other reason is a write of `kind =
KOS_CACHE_EMPTY`.

Under `KOS_EVAL_SAFETYNET` a stack-pair int body refuses a site whose
operand count is not the two it was elected for. Assert only; the
dispatch is unchanged.

An ungated site (`compiler/cache-expression` off, or `KOS_CACHE_UNSET`)
tail-calls the electing function every time, which is today's ladder plus
one classification.

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

**OBJECT_OPERAND, OBJECT_INPLACE, FAST_INPLACE, FAST_INPLACE_CONST**.
Built: the exact `PyLong` pair and the exact `PyFloat` pair per
operator, the operator a site constant indexing the family's int or
float table, the guard two borrowed type compares in the body, the
result written into the operand the stack or the raw slot alone owns;
the exact `PyUnicode` pair for `+`, appended in place into the home
that held the only reference; and on the two stack families a fold
body per operator and kind for a run of three or more, and a join
for a run of strs. `VAR_INPLACE(_CONST)` stay on the ladder; hot code
uses the fast forms.

**OBJECT_COMPARE(_VAR/_FAST), CHECK_COMPARE(_VAR/_FAST), SWITCH_EQ\***.
`kos_compare_chain` takes the selector and walks the operands; the extended
tail is out of line. Built for CHECK_COMPARE and OBJECT_COMPARE: the
exact `PyLong` pair and the exact `PyFloat` pair, one body each per
opcode reading the selector off the site, the `CHECK_*` body owning
the jump. CHECK_COMPARE_FAST keeps its inline int arm (a slot measured
as a loss). Open on the same slots: the exact `PyUnicode` pair and the
identity selectors.

**OBJECT_FORMAT(_CONST), FAST_FORMAT(_CONST), VAR_FORMAT(_CONST)**. The
fused format opcodes test `PyUnicode_CheckExact` and the spec shape per
call. Electors: exact str with empty spec, exact int, generic `__format__`.

**ITEM, ITEM_STORE, ITEM_INPLACE and every row of the item grid**.
Subscript through `PyObject_GetItem`. Electors: exact `PyDict` under any
key, exact `PyList` with `PyLong` index, exact `PyTuple`, generic
mapping. Built: the election lives in the shared helpers of
`eval/eval_item.c` on a KOS_CACHE_ITEM slot, and every row of the
family is a fetch, a call, and a settle; see
[../archive/item-opcode-matrix.md](../archive/item-opcode-matrix.md).

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
touch the instruction stream: the frame's `resume_kind` / `resume_state`
pair. A node-style resume sets that pair to a phase kind and the node
as its state. The opcode then runs once per node, and every later delivery
lands on the phase function, which reads its node, does the phase's work,
and either arms the next phase or threads on past the opcode.

That distinguishes two kinds of resume state, which the capture rule must
tell apart. A step resume's state (map, filter) is owned by the step object
and cannot be replayed, so `kos_dynframe_clone` refuses by name. A node
resume needs no stored state: the node it serves is `dyn_top` above the
frame, so it is armed with `resume_state` NULL, and that NULL is the
discriminator. The clone copies such a resume, the delivery step hands
it its value raw, and the node's pop and unwind disarm it. A bytecode
frame's delivery is always the pending slot, never its operand stack,
which is what lets a frame with live operands carry a resume at all.

### Where it pays: survey

**YIELD_FROM**. Built; the description below is what it replaced. Two
axes were folded into one 766-line handler. The kind axis (`KOS_YF_SYNC_ITER`, `SYNC_GEN`, `PY_ASYNC_ITER` /
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
in-place delimited invoke, a displaced stack. A resume kind beside the pc
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

1. **YIELD_FROM, node-style.** Done. It retired the pc-rollback
   re-entry pattern and settled the node-resume rule.
2. **CALL_FAST and CALL, cache-style.** Done and removed: flat.
   **OBJECT_OPERAND and the in-place families, cache-style.** Done, see
   the status above.
3. **FOREACH `step` and the NEXT node, node-style.** Follows the
   YIELD_FROM shape on a simpler node, with the delivery contract
   already in place.
4. **FRAME `on_return`, node-style.** Every return classifies its parent
   with three chain reads; the install knows the shape. A ladder is
   replaced rather than a call, which is where the CALL_FAST elector
   found nothing.
5. **Float bodies on the arithmetic and compare slots.** Done, see the
   status above, as are the str bodies and the fold bodies on the
   stack families.
6. **CHECK_COMPARE and OBJECT_COMPARE, cache-style.** Done, see the
   status above. CHECK_COMPARE_FAST stays on its inline arm, and the
   `_VAR` twins and `SWITCH_EQ*` stay on their ladders.
7. **FAST_ATTR_INPLACE.** Done, see the status above: the elector is
   the site's second slot beside its field cache, and the body guards
   through the field slot.

## Sizing

Neither union grows. The elector arm is smaller than the method arm of
`KosCacheSlot`, and the `with` arm is the largest of `KosDynNode`, so a
pointer on the `frame`, `yield_from`, and `foreach` arms fits; a `sizeof`
check at implementation confirms it. Should a kind ever outgrow the slot,
the assembler already reserves a run per site: the kind requests two slots
in `opcodes.incl` and its layout spans the pair.

# The end.
