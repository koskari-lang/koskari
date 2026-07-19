# Fused item in-place opcodes

## Status

Proposal -- not implemented. Six potential new opcodes pending review.

## Summary

Replace the stack-duplication choreography that `compile/inplace-item`
emits today with a family of fused read-modify-write item opcodes,
mirroring the existing attribute in-place family (`ATTR_INPLACE`,
`VAR_ATTR_INPLACE`, `FAST_ATTR_INPLACE`). Six opcodes in two tiers plus
their fast lowerings:

- **Stack-object tier**: `ITEM_INPLACE`, `CONST_ITEM_INPLACE` -- the
  object (and, for `ITEM_INPLACE`, the key) ride the stack.
- **Variable tier**: `VAR_ITEM_INPLACE`, `VAR_CONST_ITEM_INPLACE` -- the
  object is a named variable resolved through the frame namespace.
- **Fast derivations**: `FAST_ITEM_INPLACE`, `FAST_CONST_ITEM_INPLACE`
  -- the `convert_fast` lowering of the variable tier onto frame slots.

Each fused opcode performs get-item, in-place reduce, and set-item in a
single dispatch, eliminating the `DUP` and separate read/write
instructions entirely.

## Motivation

`compile/inplace-item` is the **only** special form that emits
`KOS_OP_DUP`. Its current lowering for `(compile/inplace-item OP OBJ KEY
V...)` is:

Constant key (5 instructions):

    <compile OBJ>
    DUP 1
    CONST_ITEM key
    <compile V...>
    OBJECT_INPLACE (op, 1+n)
    CONST_ITEM_STORE key
    CONST None

Computed key (5 instructions):

    <compile OBJ>
    <compile KEY>
    DUP 2
    ITEM 0
    <compile V...>
    OBJECT_INPLACE (op, 1+n)
    ITEM_STORE 0
    CONST None

The `DUP` exists solely so the object (and key) survive the read to
serve the later store. Each fused opcode collapses `DUP` + read +
`OBJECT_INPLACE` + store into one instruction: four dispatches become
one, the duplicate stack entries (and their incref/decref pairs)
disappear, and maximum stack depth shrinks.

The attribute path made this exact move already: `compile/inplace-attr`
emits a single fused `ATTR_INPLACE` / `VAR_ATTR_INPLACE`, and the read
and write-back live in one handler over the shared
`kos_inplace_reduce` core. The item path is currently the odd one out.
The in-flight `VAR_OBJECT_INPLACE_CONST` / `FAST_OBJECT_INPLACE_CONST`
work continues the same fusion direction for plain variables.

## Proposed opcodes

All six are `KOS_OP_HANDLER_COLD` (the operator switch lives in the
shared reduce core, out of the inlined dispatch loop), cache arity 0
(no per-site cache kind exists for the item protocol; see Open
questions). `OPCOUNT` carries the operator selector in the high byte
and the reduce count in the low byte, as elsewhere.

### Stack-object tier

    OP(ITEM_INPLACE, 0, OPCOUNT)

Stack `[obj, key, v0 .. v_{n-1}]`, arg = merged OPCOUNT. Read
`obj[key]`, reduce in place with the n values left-to-right, store the
result back to `obj[key]`, drop all n+2 entries. Pushes nothing (the
special appends `CONST None`). No EXTRA row needed -- same bare-arg
encoding as `OBJECT_INPLACE`.

    OP(CONST_ITEM_INPLACE, 0, EXTRA, OPCOUNT, CONST, NOARG, NOARG)

Stack `[obj, v0 .. v_{n-1}]`, arg = EXTRA row index; row a0 = OPCOUNT,
a1 = CONST (key). Same reduce and write-back; drops n+1 entries. This
is a near transliteration of `ATTR_INPLACE` with
`PyObject_GetItem` / `PyObject_SetItem` in place of the cached
attribute access, and no cache slot.

### Variable tier

    OP(VAR_ITEM_INPLACE, 0, EXTRA, OPCOUNT, VAR, NOARG, NOARG)

Stack `[key, v0 .. v_{n-1}]`; row a0 = OPCOUNT, a1 = VAR (obj name).
`obj = frame_ns_deref(name)`; then read, reduce, write-back as above.
The object never touches the stack.

    OP(VAR_CONST_ITEM_INPLACE, 0, EXTRA, OPCOUNT, VAR, CONST, NOARG)

Stack `[v0 .. v_{n-1}]`; row a0 = OPCOUNT, a1 = VAR (obj name), a2 =
CONST (key). The fully-resolved shape: only the reduction values ride
the stack. Schema-identical to `VAR_ATTR_INPLACE` (minus the cache).

### Fast derivations

    OP(FAST_ITEM_INPLACE, 0, EXTRA, OPCOUNT, FASTVAR, NOARG, NOARG)
    OP(FAST_CONST_ITEM_INPLACE, 0, EXTRA, OPCOUNT, FASTVAR, CONST, NOARG)

Identical semantics with the object read from a frame fast slot. The
`convert_fast` lowering map is built by name
(`KOS_OP_FAST*` -> `replace('FAST', 'VAR', 1)`), so both pairs --
`VAR_ITEM_INPLACE` -> `FAST_ITEM_INPLACE` and `VAR_CONST_ITEM_INPLACE`
-> `FAST_CONST_ITEM_INPLACE` -- participate automatically once the
names exist; no additions to the hand-maintained map entries.

## Emission from `compile/inplace-item`

The special selects by the same two axes it already inspects (is the
object a bare symbol; does the key compile to a lone constant),
mirroring `special_inplace_attr`:

| obj      | key      | emit                                          |
|----------|----------|-----------------------------------------------|
| symbol   | constant | `VAR_CONST_ITEM_INPLACE ((op, n), name, key)` |
| symbol   | computed | `<key>` `<v...>` `VAR_ITEM_INPLACE ((op, n), name)` |
| expr     | constant | `<obj>` `<v...>` `CONST_ITEM_INPLACE ((op, n), key)` |
| expr     | computed | `<obj>` `<key>` `<v...>` `ITEM_INPLACE (op, n)` |

All four are followed by `CONST None`, as today. The variable rows use
`frag.request_var(name)` (the binding is only read; the mutation
targets the object's item, not the binding), matching the attr special.
`OBJECT_INPLACE`'s count remains `1+n` in the old encoding because the
read item rode the stack as the accumulator seed; in the fused forms
the count is just `n` and the handler seeds the reduce with the value
it read itself.

## Handler design

One shared helper, the item analog of `kos_attr_inplace_cached` without
the cache slot:

    int kos_item_inplace(
        PyObject *obj, PyObject *key,
        KosOperand op, PyObject *const *values, int count);

get-item, seed `kos_inplace_reduce` with the read value over the count
operands, set-item, with the usual error propagation at each step. All
six handlers are thin wrappers differing only in where obj and key come
from (stack tail, EXTRA VAR deref, or fast slot).

## Semantics: the read moves

Today the `ITEM` / `CONST_ITEM` read fires **before** the value
expressions evaluate, matching CPython's augmented-assignment order
(`a[i] += rhs` loads `a[i]` before running `rhs`). In every fused form
the values are already on the stack when the instruction fires, so the
read happens **after** value evaluation. Observable when a value
expression mutates `obj[key]` (the fused form sees the mutation) or
raises (the fused form never performs the read or its side effects).

The variable tier adds the same shift for the object itself: the deref
happens at instruction time, after the key and values evaluate, so a
value expression that rebinds the variable changes which object is
mutated, and an unbound name surfaces later than it does today.

`inplace-attr` already accepted exactly this trade when it fused
(`ATTR_INPLACE` reads the attribute after the values evaluate), so the
change makes the two families consistent with each other at the cost of
both diverging from CPython ordering the same way. This should be
pinned deliberately: a corpus record asserting read-at-op-time ordering
for both `inplace-item` and `inplace-attr`, and a note in the VM
reference.

## Costs and obligations

- **Dispatch sizing**: +6 opcodes against the budget tracked in
  [../dispatch-sizing.md](../dispatch-sizing.md). All six are cold
  handlers, so the hot dispatch loop does not grow.
- **`.kio` ABI**: inserting the rows shifts subsequent opcode values;
  `BYTECODE_ABI_MINOR` bump in `koskari/serialize/constants.py`
  (precedent: the attr in-place family did the same at 0.7).
- **Regeneration**: `handlers.incl` via `tox -e handlers`.
- **Docs**: per-opcode pages under `docs/reference/vm/opcodes/`, and
  [../OPCODES.md](../OPCODES.md).
- **Peephole**: no interaction. The `DUP`-before-`POP` elision in
  `compress` keeps its other users; `compile/inplace-item` simply stops
  emitting `DUP`, leaving zero special-form emitters of it (hand
  assembly still can).

## Testing

- `phase_lib` opcode tests for each handler, hand-assembled VAR-first
  (VAR rows with names, letting `convert_fast` assign slots).
- `phase_specials` coverage of the four emission shapes, including
  multi-value reduction and the inplace-`**` single-value restriction.
- A reproducer or corpus record pinning read-at-op-time ordering (both
  the mutating-value-expression and raising-value-expression cases).

## Open questions (for the six-opcode review)

1. **Is the stack-object tier worth two opcodes?** In real code the
   object is usually a named local, which the variable tier captures.
   The stack tier exists for expression objects
   (`(inplace-item + (get-store) k 1)`) and for compile shapes the
   resolver cannot claim. `ATTR_INPLACE` kept its stack form; symmetry
   argues for keeping both, but the review should confirm the tier
   carries its dispatch-table weight.
2. **Naming**: `VAR_CONST_ITEM_INPLACE` follows the `CONST_ITEM*`
   family prefix; the in-flight variable work instead suffixes
   (`VAR_OBJECT_INPLACE_CONST`, where `_CONST` marks a constant RHS
   operand rather than a constant key). Both spellings survive the
   FAST/VAR name mapping; pick one story for "constant key" vs
   "constant operand" before the enum lands.
3. **Further fusion**: a constant single RHS could later fold into the
   EXTRA row (the analog of `VAR_OBJECT_INPLACE_CONST`), reaching a
   fully stack-free `d[k] += 1`. Out of scope here; noted so the row
   schemas leave room (`VAR_CONST_ITEM_INPLACE` already uses three of
   four EXTRA fields).
4. **Item caching**: no per-site cache kind exists for the item
   protocol. If a workload ever justifies one (e.g. dict-specialized
   get/set), these sites would be the natural carriers; cache arity 0
   for now keeps that door open without paying for it.

## Related

- [../../koskari/compile/specials/object.py](../../koskari/compile/specials/object.py)
  -- `special_inplace_item`, `special_inplace_attr`.
- `koskari/lib/opcode/ATTR_INPLACE.c`, `VAR_ATTR_INPLACE.c`,
  `FAST_ATTR_INPLACE.c` -- the fused attr family this mirrors.
- `koskari/lib/operator.c/h` -- `kos_inplace_reduce`,
  `kos_attr_inplace_cached`.
- [../dispatch-sizing.md](../dispatch-sizing.md) -- opcode-count budget.
