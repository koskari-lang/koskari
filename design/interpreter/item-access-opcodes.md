# Variable-tier item access opcodes and item electors

## Status

Implemented on the churn-test-performance branch (2026-09-16): the
eight rows, the slot on ITEM, CONST_ITEM, ITEM_STORE and
CONST_ITEM_STORE and on the four FAST rows, the elector family, the
two fuse rules, and the set-item emission; then the constant-value
stores VAR_ITEM_STORE_CONST and VAR_CONST_ITEM_STORE_CONST with their
FAST twins (schema VAR, VAR|CONST key, CONST value, NOARG), squashed
from a CONST ahead of the fused store as VAR_STORE_CONST is, their
list body taking a new reference to the constant instead of the
stack's. One departure from the
proposal below: the VAR rows carry no slot. A VAR row survives
lowering only for a name with no fast slot, and its cost is the name
resolution, so a body would guard behind a dict lookup; the schema
table and the cache-arity item under Costs describe the rows as
proposed. Measured on the churn harness against the 2026-09-16
baseline: fannkuch 1.87x to 1.48x of CPython, binary_trees unchanged
at 2.09x. Companion to
[item-inplace-opcodes.md](item-inplace-opcodes.md), which covers the
read-modify-write shape; the two share the object and key axes and
the elector bodies.

## Summary

The item family is object-on-stack only. Every `x[i]` in a function
body is two dispatches (a fast load, then ITEM) and every `set! x[i]
v` is three or more, and each one goes through `PyObject_GetItem` or
`PyObject_SetItem` with the mapping-protocol dispatch and the index
conversion inside. The attribute family solved both halves already:
VAR_ATTR and FAST_ATTR take the object from a name, and the site
carries a cache slot. This proposal gives the item family the same
two things.

- **VAR_ITEM, VAR_CONST_ITEM, VAR_ITEM_STORE, VAR_CONST_ITEM_STORE**
  and their FAST twins, delivered by the compress pipeline's fuse
  rules for loads and by the set-item special for stores.
- **A `KOS_CACHE_ELECTOR` slot** on ITEM, CONST_ITEM, ITEM_STORE,
  CONST_ITEM_STORE and on the eight new rows, with one elected body
  per container kind: exact list, exact tuple, exact dict, generic.

## Motivation

The fannkuch churn kernel under `tox -qe churn-trace -- fannkuch`
(382M dispatches, 5 samples of 50 runs):

| op | count | share of time | avg |
|---|---|---|---|
| ITEM_STORE | 31.8M | 7.5% | 8ns |
| ITEM | 30.7M | 6.9% | 8ns |
| CONST_ITEM | 9.7M | 2.1% | 7ns |
| FAST2 / FAST3 / FAST4 | 45.0M | 9.5% | 7ns |

The FAST2, FAST3 and FAST4 rows are almost entirely feeders of the
item rows. The kernel's inner swap is:

    FAST2      (perm, i)
    ITEM
    FAST_STORE tmp
    FAST4      (perm, i, perm, j)
    ITEM
    ITEM_STORE
    FAST3      (perm, j, tmp)
    ITEM_STORE

Eight dispatches; the fused shape is five:

    FAST_ITEM        (perm, i)
    FAST_STORE       tmp
    FAST_ITEM        (perm, j)
    FAST_ITEM_STORE  (perm, i)
    FAST             tmp
    FAST_ITEM_STORE  (perm, j)

The averages above sit at the dispatch floor of the trace build, so
the dispatch count is the first cost and the `PyObject_GetItem` path
is the second. CPython 3.12 runs the same kernel with
`BINARY_SUBSCR_LIST_INT` and `STORE_SUBSCR_LIST_INT`: a type check, a
bounds check, and a direct cell access. The elector bodies are that
specialisation on the electors framework.

## Opcode inventory

The item family today, by object source and key source:

| object | key | load | store | clear | take | ref |
|---|---|---|---|---|---|---|
| stack | stack | ITEM | ITEM_STORE | ITEM_CLEAR | ITEM_TAKE | ITEM_REF |
| stack | CONST | CONST_ITEM | CONST_ITEM_STORE | CONST_ITEM_CLEAR | CONST_ITEM_TAKE | CONST_ITEM_REF |

The rows this proposal adds:

| object | key | load | store |
|---|---|---|---|
| VAR | VAR | VAR_ITEM | VAR_ITEM_STORE |
| VAR | CONST | VAR_CONST_ITEM | VAR_CONST_ITEM_STORE |
| FAST | FAST | FAST_ITEM | FAST_ITEM_STORE |
| FAST | CONST | FAST_CONST_ITEM | FAST_CONST_ITEM_STORE |

Clear, take and ref stay stack-object only: `del x[i]` and
`(take-item x i)` are rare and the two-dispatch shape is fine for
them. A VAR object with a computed key (`count[(- r 1)]`) also stays
on the stack tier: the load ahead of ITEM is one dispatch, and a row
with one operand from a slot and one from the stack would be a third
schema for the same body.

The in-place rows are specified in
[item-inplace-opcodes.md](item-inplace-opcodes.md); with this proposal
they take the same cache slot and elector bodies (see Electors).

### Row schemas

    OP(VAR_ITEM,              1, EXTRA, VAR,     VAR,   NOARG, NOARG)
    OP(VAR_CONST_ITEM,        1, EXTRA, VAR,     CONST, NOARG, NOARG)
    OP(VAR_ITEM_STORE,        1, EXTRA, VAR,     VAR,   NOARG, NOARG)
    OP(VAR_CONST_ITEM_STORE,  1, EXTRA, VAR,     CONST, NOARG, NOARG)
    OP(FAST_ITEM,             1, EXTRA, FASTVAR, FASTVAR, NOARG, NOARG)
    OP(FAST_CONST_ITEM,       1, EXTRA, FASTVAR, CONST,   NOARG, NOARG)
    OP(FAST_ITEM_STORE,       1, EXTRA, FASTVAR, FASTVAR, NOARG, NOARG)
    OP(FAST_CONST_ITEM_STORE, 1, EXTRA, FASTVAR, CONST,   NOARG, NOARG)

Field 0 is the object, field 1 the key. The existing four rows gain
the slot:

    OP(ITEM,             1, NOARG)
    OP(ITEM_STORE,       1, NOARG)
    OP(CONST_ITEM,       1, CONST)
    OP(CONST_ITEM_STORE, 1, CONST)

### Stack contracts

- Loads push the item; nothing is popped. An unbound object or key
  name raises NameError as FAST and VAR do.
- Stores pop the value (`[..., val]` to `[...]`) and push nothing; the
  special appends `CONST None` as it does today.
- ITEM and CONST_ITEM keep their contracts; the slot changes nothing
  observable.

The naming follows the `CONST_ITEM*` prefix convention and the
item-inplace doc's `VAR_CONST_ITEM_INPLACE`; the FAST twins are the
name-derived lowering, so `VAR_CONST_ITEM` becomes `FAST_CONST_ITEM`
without an entry in the hand-maintained map.

## Delivery

Two mechanisms, chosen by what the compress pipeline can see.

### Loads: fuse rules

The compress pipeline's `fuse_feeders` phase absorbs bare VAR and
CONST loads that immediately precede a consumer. Two rules:

    VAR obj, VAR key, ITEM          ->  VAR_ITEM (obj, key)
    VAR obj, CONST_ITEM k           ->  VAR_CONST_ITEM (obj, k)

`VAR obj, CONST k, ITEM` never reaches the rule set because the item
special already emits CONST_ITEM for a constant key. `CONST obj, VAR
key, ITEM` (a constant indexed by a name) is declined. Fusion runs
before `convert_fast`, so a local object lowers to FAST_ITEM and a
module-level or closure object stays VAR_ITEM, still saving the
dispatch. The trailing `pack_runs` phase sees only the feeders the
rules declined, so FAST2 and friends keep forming where no item row
consumed the loads.

The rules also catch hand-assembled and other-emitter streams, which
is the reason to prefer them over emission from the item special for
the load shapes.

### Stores: the set-item special

The set-item special emits object, key, then value, so the value rows
sit between the feeders and ITEM_STORE and the fuse driver cannot
reach across them. Rather than teach the driver to look past
arbitrary rows, the special emits the fused shape itself when the
object is a bare symbol and the key is a symbol or a constant:

    <value rows>
    VAR_ITEM_STORE (obj, key)          ; symbol key
    VAR_CONST_ITEM_STORE (obj, k)      ; constant key
    CONST None

Any other shape keeps today's emission. This is the same selection
the inplace-attr special makes and the item-inplace doc proposes for
inplace-item.

The alternative, a fuse rule that accepts a single-row pusher
(VAR_ITEM, VAR_ATTR, a CONST) as the value feeder and re-emits it
ahead of the fused store, covers fewer shapes and moves the object
and key loads after the value load. The special is the recommendation.

### Ordering

In the fused store the object and key names are dereferenced when
the row executes, after the value expression has run. Today they are
loaded first. The observable differences are the same two the
item-inplace doc accepts for its variable tier: a NameError on the
object or key surfaces after the value expression has run its side
effects, and a value expression that rebinds the object name stores
into the new binding. CPython evaluates `a[k] = v` in the order v, a,
k, so the fused shape matches Python where today's shape does not. A
corpus record should assert the order for set-item alongside the one
the item-inplace doc asks for.

Loads have no ordering question: both operands are names.

## Electors

The item family joins the cache-style electors of
[electors.md](electors.md): the handler loads the site's
`KOS_CACHE_ELECTOR` slot and tail-calls the elected body; the body
guards on every execution; a guard miss tail-calls the shared miss
function, which runs the generic body and counts toward the demotion
limit of 8; the generic body empties the slot every 256 executions so
a demoted site re-elects. The electing function classifies the site's
first container and key and settles the slot.

### Bodies

One body per container kind and access direction. The container is
classified by exact type; subclasses take the generic body because
they may override `__getitem__`.

| kind | guard | load | store |
|---|---|---|---|
| list_int | `PyList_CheckExact(obj) && PyLong_CheckExact(key)` | index read, negative normalise, bounds check, `PyList_GET_ITEM` + incref | same checks, then swap the cell and release the old value |
| tuple_int | `PyTuple_CheckExact(obj) && PyLong_CheckExact(key)` | as list_int over `PyTuple_GET_ITEM` | none (tuples do not store; the site takes generic) |
| dict | `PyDict_CheckExact(obj)` | `PyDict_GetItemWithError`; a missing key falls to generic | `PyDict_SetItem` |
| generic | none | `PyObject_GetItem` | `PyObject_SetItem` |

The index read is the inline exact-int read the arithmetic bodies use
(the two-digit fast read with the general `PyLong_AsSsize_t` behind
it). An index outside the sequence falls to the generic body for that
execution rather than raising in the elected body, so IndexError and
KeyError keep CPython's messages and the elector owns no error text.
A miss on the bounds check is not a guard miss and does not count
toward demotion: the container kind is still right.

Reads take the operands borrowed from the slots, as CHECK_COMPARE_FAST
peeks its pair; nothing between the peek and the access can rebind a
slot. Stores hold the value the stack owns and transfer it into the
cell.

### In-place rows

The in-place rows from the item-inplace doc take two slots, as
FAST_ATTR_INPLACE does: the container elector first, the operand
elector second. The container body reads the cell, the operand body
computes with the cell's value as the in-place candidate under the
existing rule (an exact int or float holding the only reference is
rewritten in place), and the container body stores back only when a
new object came back. A list cell that holds the only reference to
its int makes `incr! count[r]` allocation-free.

### Str containers

`str[int]` returns a new one-character string on every access, so a
str body would save the mapping dispatch and nothing else. Left to
the generic body until a workload shows it.

### What the elector does not change

Every fallback is the generic body, which is today's handler out of
line, so a site whose container is a user mapping, a subclass, or a
mix of kinds runs exactly what it runs now plus the slot load. The
family stays interface-only: the handler is a slot load and a tail
call; the bodies are the only place a container type is named.

## Costs and obligations

- **Dispatch table**: +8 opcodes. Each new handler is a slot load and
  a tail call, the same size as the arithmetic prologues.
- **Cache arity**: ITEM, CONST_ITEM, ITEM_STORE, CONST_ITEM_STORE go
  from 0 to 1 slot. Code objects size their slot arrays from the
  table, so existing `.kio` files need the ABI bump below.
- **`.kio` ABI**: new rows shift later opcode values;
  `BYTECODE_ABI_MINOR` bump in `koskari/serialize/constants.py`.
- **Regeneration**: `handlers.incl` via `tox -e handlers`.
- **Compiler**: two fuse rules in `koskari/compile/processor.py`; the
  set-item special in `koskari/compile/specials/items.py` gains the
  symbol-object branch. `convert_fast` needs no entry.
- **Docs**: per-opcode pages under `docs/reference/vm/opcodes/`,
  [../OPCODES.md](../OPCODES.md) section 15, the families page, and
  the electors section of [../EVALUATOR.md](../EVALUATOR.md).
- **dis**: the row schemas are table-driven, so the annotations
  follow; the elector kind should print in the cache dump as the
  arithmetic electors do.

## Testing

- `phase_lib` opcode tests for the eight rows, hand-assembled
  VAR-first with names, letting `convert_fast` assign slots; each
  covers the three elected kinds and the generic fallback, an
  out-of-range index, a missing dict key, a subclass container, and an
  unbound name.
- Elector lifecycle tests: election on first execution, guard miss
  after the container changes kind, demotion at the limit, re-election
  after 256 generic executions. Same shape as the arithmetic elector
  tests.
- `phase_compile` tests for the two fuse rules and their declines,
  and `phase_specials` tests for the four set-item emission shapes.
- A corpus record for set-item evaluation order (value first, then
  object and key deref).
- The ASan suite is a gate for this step: the store bodies write into
  live list cells and dict entries.

## Open questions

1. **A stack-key row for a VAR object** (`count[(- r 1)]`): one
   dispatch saved per computed-key access at the cost of two more
   opcodes and a third body signature. Measure after the eight rows
   are in; fannkuch has two such sites against six symbol-key sites.
2. **Election timing for stores** whose first execution sees an empty
   list: the guard is on the container type, not its length, so
   nothing changes; recorded because the attr family had a first-run
   subtlety here.
3. **Whether VAR_ITEM should also absorb `VAR obj, VAR key, ITEM_REF`**
   for the ref family. No workload asks for it.

## Related

- [item-inplace-opcodes.md](item-inplace-opcodes.md) -- the
  read-modify-write rows; shares the axes and the bodies.
- [electors.md](electors.md) -- the elector contract and the measured
  history of the arithmetic, compare, and attr families.
- `koskari/lib/opcode/ITEM.c`, `ITEM_STORE.c`, `CONST_ITEM.c` -- the
  handlers that become generic bodies.
- `koskari/lib/opcode/FAST_ATTR.c`, `FAST_ATTR_INPLACE.c` -- the
  attribute rows this mirrors.
- `koskari/compile/processor.py` -- `fuse_rule`, `fuse_feeders`,
  `convert_fast`, `pack_runs`.
- `examples/churn/ki/fannkuch.ki` -- the kernel the numbers come from.
