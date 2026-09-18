# Item opcode matrix: naming, shared helpers, and the full grid

**Status:** Implemented 2026-09-18 on the fast-item-store-opcodes branch: the 92 rows, `eval/eval_item.c`, the fold rules, the specials, the ABI bump to 0.19, the tests, and the references. The container runs (native, ASan, valgrind) are the remaining gates.

**Ref:** [electors.md](electors.md) for the cache-style elector home the
item families use today; [../OPCODES.md](../OPCODES.md) sections 12, 13
and 15 for the current rows; `koskari/lib/eval/eval_electors.c` for the
families being replaced.

## Scope

Three changes to the item family, taken together because each reshapes
the same files:

1. Rename every item row to a three-position grammar that spells the
   origin of the object, the key, and the value.
2. Replace the per-family elector bodies with three shared helpers,
   `kos_item_load`, `kos_item_store`, and `kos_item_inplace`, that own
   the election, the container bodies, and the protocol fallback. A
   handler fetches its operands, calls one helper, and settles.
3. Fill the grid: every combination of object in {stack, slot} and key
   in {stack, const, slot} for every verb, value in {stack, const, slot}
   for stores, operand in {stack, const} for in-place rows, with a VAR
   twin for every row that names a slot.

The arc is one change. Nothing in it is staged for a later arc.

## Naming grammar

```
[origin_]ITEM[_origin]                      load
[origin_]ITEM[_origin]_STORE[_origin]       store
[origin_]ITEM[_origin]_INPLACE[_CONST]      in-place reduce
[origin_]ITEM[_origin]_{CLEAR,TAKE,REF}     delete, take, ref
```

The leading marker names the object. Every verb is followed by the
origin of its own operand: ITEM takes a key, STORE and INPLACE take a
value. An unmarked operand rides the stack. The markers are CONST for a
pool constant, VAR for a name resolved in the frame namespace, and FAST
for a fast slot. A VAR row lowers to its FAST twin when every named
operand has a fast slot, all or nothing, as today.

The rule the current names carry, an unmarked key sharing the object's
origin, is gone. Under the new grammar FAST_ITEM means a fast object
under a stack key, which is a row the grid adds.

The row's EXTRA fields follow name order, object then key then value,
skipping stack operands. A row with one named operand uses that
operand's plain style (CONST, VAR, FASTVAR) instead of EXTRA, as
CONST_ITEM does today.

## The grid

Object in {stack, slot} and key in {stack, const, slot} for every verb;
value in {stack, const, slot} for stores; operand in {stack, const} for
the in-place rows. A slot operand has a FAST row and a VAR twin. A
const object has no row, and an attribute or any other compound object
is a stack object one row ahead. Loads have 6 cells, stores 18,
in-place 12, and CLEAR, TAKE, and REF 6 each.

The in-place operand set is the VM-wide INPLACE rule: FAST_INPLACE,
FAST_ATTR_INPLACE, and every other in-place row take the operand from
the stack or the pool, and a slot operand is a FAST feeder the pipeline
leaves ahead of the row. The item rows follow that rule rather than
open a third operand origin the other in-place families lack.

| Group | FAST-side rows | VAR twins | Exists today |
|---|---|---|---|
| load, stack obj | ITEM, ITEM_CONST, ITEM_FAST | ITEM_VAR | ITEM, ITEM_CONST |
| load, slot obj | FAST_ITEM, FAST_ITEM_CONST, FAST_ITEM_FAST | 3 | FAST_ITEM_CONST, FAST_ITEM_FAST |
| store, stack obj | 9 | 5 | ITEM_STORE, ITEM_CONST_STORE |
| store, slot obj | 9 | 9 | 4 of each |
| in-place, stack obj | 6 | 2 | ITEM_INPLACE, ITEM_CONST_INPLACE |
| in-place, slot obj | 6 | 6 | 4 of each |
| clear, take, ref, stack obj | 9 | 3 | 6 |
| clear, take, ref, slot obj | 9 | 9 | none |

New rows: 60. Opcode count 246 to 306; the op field is a uint16.

## Shared helpers

Three static inline functions in a new fragment `eval/eval_item.c`,
included from `eval.c` beside `eval_electors.c`, which keeps the
numeric, compare, and attr families where they are.

```c
/* obj and key borrowed. Returns a new reference, or NULL with the
   error set. */
static inline PyObject *kos_item_load(
    KosCacheSlot *slot, PyObject *obj, PyObject *key);

/* obj and key borrowed. val stolen: consumed on every path, the
   error path included. Returns 0, or -1 with the error set. */
static inline int kos_item_store(
    KosCacheSlot *slot, PyObject *obj, PyObject *key, PyObject *val);

/* obj and key borrowed. args borrowed: count operands, on the stack
   or one pool constant. obj[key] = obj[key] op args..., reduced left
   to right. Returns 0, or -1 with the error set. */
static inline int kos_item_inplace(
    KosCacheSlot *slot, PyObject *obj, PyObject *key, KosOperand op,
    PyObject *const *args, int count);
```

The slot is passed, not the insn, so the helper reads no row layout and
a two-slot site can hand over `slot + 1`. A NULL slot is an ungated
site: the helper classifies on every execution and runs the body it
chose, so the VAR twins, which carry no slot, still take the container
bodies.

Three more, for the verbs with no container body to elect:

```c
/* obj and key borrowed, held across the call. */
static inline int kos_item_clear(PyObject *obj, PyObject *key);
/* obj and key borrowed, held. Returns the item as a new reference
   once it is deleted, or NULL with the error set. */
static inline PyObject *kos_item_take(PyObject *obj, PyObject *key);
/* obj and key borrowed. Returns the new KosItemRef. */
static inline PyObject *kos_item_ref(PyObject *obj, PyObject *key);
```

They exist so that a handler's peek-borrowed operand is held across
`PyObject_DelItem` and `PyObject_GetItem`, which run user code, by the
same rule the elected helpers apply on their protocol paths. No slot.

Only the elected hit path is inline. The election, the protocol path,
and the miss accounting are `KOS_NOINLINE` functions, so a handler
grows by the slot test, the body call, and the switch on its code, and
the cold paths are one copy each. The elector families found that an
inlined generic-body lookup grew every body by a fifth and cost cycles
(electors.md, survey).

### Ownership rule

Stolen value is what makes every origin free of reference traffic:

| Value origin | What the handler does | Cost beyond the cell's own reference |
|---|---|---|
| stack | pop without decref; the reference moves | none |
| const | `Py_INCREF` | none; the cell needs one |
| slot | `kos_ns_fast_deref`, which increfs on a hit | none; one call |

Borrowed object and key are safe across the list body, which runs no
user code between fetch and access. The dict body and the protocol
fallback can run user code that rebinds a fast slot, so the helper
holds obj and key across those two paths and only those, as the dict
body does today. Stack and const operands pay that hold needlessly on
the dict path; that is today's behavior too.

### Slot representation

A new cache kind, `KOS_CACHE_ITEM`, whose payload mirrors the elector
struct with a body of the item signature instead of a `KosThreadFn`:

```c
struct {
  union {
    KosItemLoadBody load;      /* int (*)(PyObject *obj, PyObject *key,
                                          PyObject **out) */
    KosItemStoreBody store;    /* int (*)(PyObject *obj, PyObject *key,
                                          PyObject *val) */
    KosItemInplaceBody inplace; /* int (*)(PyObject *obj, PyObject *key,
                                           PyObject *rhs) */
  } fn;
  uint8_t misses;              /* guard misses since the election */
  uint8_t mode;                /* KosElectorMode */
  uint8_t ticks;               /* protocol executions; wrap re-elects */
} item;
```

No owned references; the teardown arm is a no-op. The miss limit,
demotion to the protocol body, and the tick wrap that empties the slot
are the elector rules unchanged, in item-typed copies of
`kos_elector_elect`, `kos_elector_miss`, and
`kos_elector_generic_tick`. The trace counters KOS_OC_ELECTOR_HIT,
MISS, BYPASS, and UNCACHED are reused.

The body is a function pointer, not a kind enum switched inline, so
the bodies stay one copy each and a handler's hot path is one indirect
call, the same count as the prologue tail it replaces. An enum would
let the compiler inline the list body into every handler, at the code
growth the elector survey measured.

A body answers one of four codes:

| Code | Meaning | Helper's next step |
|---|---|---|
| KOS_ITEM_HIT | done | return |
| KOS_ITEM_MISS | guard failed (wrong container kind) | count the miss, run the protocol |
| KOS_ITEM_DECLINE | right kind, cell out of range or absent | run the protocol, no count |
| KOS_ITEM_ERROR | error set | return the failure |

### Bodies

Five static functions, written once:

| Body | Guard | Access |
|---|---|---|
| list load | exact list, exact int key in range | `PyList_GET_ITEM` + incref |
| tuple load | exact tuple, exact int key in range | `PyTuple_GET_ITEM` + incref |
| dict load | exact dict | hold obj and key; `PyDict_GetItemWithError`; absent is a decline |
| list store | exact list, exact int key in range | `PyList_SET_ITEM` takes the stolen val; old released last |
| dict store | exact dict | hold obj and key; `PyDict_SetItem`; decref val after |

Index normalization is `kos_item_index` as today. A key wider than long
long or outside the sequence is a decline.

The protocol paths: `PyObject_GetItem` and `PyObject_SetItem` with obj
and key held, followed by the tick. The store path decrefs val after.

The in-place bodies are the numeric elector bodies as they stand, with
the family dimension removed: one int table and one float table
indexed by operator, and one str add, each of the inplace signature
with the same four codes. Inside a body: `kos_item_inplace_cell` finds
the cell (list under an exact int index, dict under any key; a wrong
kind is a miss, an index outside the list or an absent dict key a
decline), the numeric guard on the cell's value and on rhs, the
arithmetic with the write-in-place rule when the container is the
value's only owner, and `kos_item_inplace_store` back into the cell.
Today those bodies exist once per family, kind, and operator through
`KOS_NUMERIC_ELECTORS`; the helper makes the family dimension the
handler's, so the tables are instantiated once.

`kos_item_inplace` runs the elected body only when count is 1. A count
above one, a miss, and a decline take `kos_item_inplace_reduce` over
args, which is the object protocol through `PyNumber_InPlace*`, as
today. The rhs type is guarded in the body on every execution; a pool
constant's type cannot change, and the guard is one exact-type compare,
so the const rows carry no separate unguarded body.

### Helper flow

```c
static inline int kos_item_store(
    KosCacheSlot *slot, PyObject *obj, PyObject *key, PyObject *val) {

  if (likely(slot && slot->kind == KOS_CACHE_ITEM)) {
    switch (slot->u.item.fn.store(obj, key, val)) {
      case KOS_ITEM_HIT:
        KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);
        return 0;
      case KOS_ITEM_ERROR:
        return -1;
      case KOS_ITEM_MISS:
        kos_item_miss(slot);
        break;
      case KOS_ITEM_DECLINE:
        break;
    }
    return kos_item_store_protocol(slot, obj, key, val);
  }

  /* empty slot or ungated site: classify once, settle the slot, and
     run the body it chose; the protocol body when nothing matched */
  return kos_item_store_elect(slot, obj, key, val);
}
```

Classification is `kos_elector_item_body` as it stands, returning the
body for the kind: dict under any key, list or tuple under an exact int
key, else NULL for the protocol body. `kos_item_store_elect` settles the
slot then runs the chosen body through the same switch, so a first
execution behaves as an elected one.

A demoted slot holds the protocol body as its elected one, so the
switch above runs it directly and the tick counts inside it.

### Operand fetch in a handler

Per origin, the fetch and release a handler writes:

| Origin | Role | Fetch | Release |
|---|---|---|---|
| stack | obj or key | `kos_stack_tail_ptr_fast` after any value pop; borrowed | drop the entries after the call, or replace them with a load's result |
| stack | value | `kos_stack_pop_ref_fast`; owned, handed to the helper | none |
| const | obj, key, or value | `KosOp_OBJ` or `KosExtra_OBJ`; borrowed | value: `Py_INCREF` before the call |
| slot, peek hit | obj or key | `kos_ns_fast_peek`; borrowed | none |
| slot, peek miss | obj or key | `kos_ns_fast_deref` into an owned shadow | `Py_XDECREF` the shadow after the call |
| slot | value | `kos_ns_fast_deref`; owned | none |

The peek miss is an empty raw slot, which raises NameError through the
deref, or a ref kind the peek does not read, which the deref resolves to
an owned reference. A captured variable takes the shadow path every
time; that is inherent and matches the generic-body fall-through today.
One macro covers the two-state operand:

```c
/* Bind obj to the slot's value, borrowed on a peek hit; on a miss,
   bind obj_ref to the owned deref and obj to it. obj_ref is NULL on
   the hit path and is what the handler releases. */
#define KOS_NS_FAST_BORROW(obj, obj_ref, ns, idx, on_error)            \
  do {                                                                  \
    (obj_ref) = NULL;                                                   \
    (obj) = kos_ns_fast_peek((ns), (idx));                              \
    if (unlikely(! (obj))) {                                            \
      (obj_ref) = kos_ns_fast_deref((ns), (idx));                       \
      if (! (obj_ref)) { on_error; }                                    \
      (obj) = (obj_ref);                                                \
    }                                                                   \
  } while (0)
```

### Handler order

1. `KOS_OP_REQUIRE_NS` when any operand is a slot.
2. Resolve slot operands: obj and key through `KOS_NS_FAST_BORROW`,
   value through `kos_ns_fast_deref`. A failure here releases the
   shadows taken so far and raises; the stack is untouched.
3. Pop the stack value, if the row has one.
4. Borrow the remaining stack operands through one tail pointer.
5. Incref a const value.
6. Call the helper.
7. Release the shadows.
8. On failure raise; the stack entries stay for the unwind, as the
   bodies leave them today.
9. Drop the stack entries a store or in-place row consumed, or settle a
   load's result:
   `RESULT_STEAL` when no operand was on the stack,
   `kos_item_replace_top` for one, `kos_stack_replace2_fast` for two.
10. `RESULT_NEXT`.

Step 3 before step 4 keeps the tail pointer aimed at obj and key with
the value already gone. Nothing between step 4 and step 9 pushes on
this frame's stack: user code inside the helper runs in its own frame.

Sketch of a three-origin row:

```c
KOS_OP_HANDLER(FAST_ITEM_CONST_STORE_FAST) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = KosEvalState_NAMESPACE(es);
  PyObject *obj, *obj_ref, *val;
  int rc;

  KOS_OP_REQUIRE_NS(ns);
  KOS_NS_FAST_BORROW(obj, obj_ref, ns, KosExtra_IMM(row, 0),
                     RESULT_RAISE());
  val = kos_ns_fast_deref(ns, KosExtra_IMM(row, 2));
  if (unlikely(! val)) {
    Py_XDECREF(obj_ref);
    RESULT_RAISE();
  }
  rc = kos_item_store(KosOp_SLOT(insn), obj, KosExtra_OBJ(row, 1), val);
  Py_XDECREF(obj_ref);
  if (unlikely(rc < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}
```

## Opcodes

Each entry: current name, new name, row style and field order, stack on
entry (top on the right), and the fetch, call, and settle. "shadow"
means the KOS_NS_FAST_BORROW two-state operand; "deref" means an owned
`kos_ns_fast_deref`; "name" means `frame_ns_deref`, owned, in a VAR
row. A VAR twin follows the FAST row's entry with its slot operands
resolved by name instead; it is not listed separately unless its shape
differs.

### Loads, stack object

#### ITEM

- Current: ITEM. Style NOARG, one cache slot.
- Stack: `[obj, key]`.
- Fetch: tail pointer of 2, both borrowed.
- Call: `kos_item_load(slot, obj, key)`.
- Settle: `kos_stack_replace2_fast` with the result.

#### ITEM_CONST

- Current: CONST_ITEM. Style CONST (key), one cache slot.
- Stack: `[obj]`.
- Fetch: tail pointer of 1; key `KosOp_OBJ`.
- Call: `kos_item_load`.
- Settle: `kos_item_replace_top`.

#### ITEM_FAST

- New. Style FASTVAR (key), one cache slot. VAR twin ITEM_VAR, style
  VAR.
- Stack: `[obj]`.
- Fetch: tail pointer of 1; key shadow (twin: name).
- Call: `kos_item_load`.
- Settle: `kos_item_replace_top`; release the key shadow before.
- Emitted for `(f x)[i]`, `grid[y][x]` (the inner load), `self.a[k]`.

### Loads, slot object

#### FAST_ITEM

- New. Style FASTVAR (obj), one cache slot. VAR twin VAR_ITEM, style
  VAR.
- Stack: `[key]`.
- Fetch: obj shadow; tail pointer of 1 for the key.
- Call: `kos_item_load`.
- Settle: `kos_item_replace_top`.
- Emitted for `lst[(+ i 1)]`. Today that pushes the list first and runs
  ITEM.

#### FAST_ITEM_CONST

- Current: FAST_CONST_ITEM. Style EXTRA (FASTVAR obj, CONST key), one
  cache slot. VAR twin VAR_ITEM_CONST, current VAR_CONST_ITEM.
- Stack: none.
- Fetch: obj shadow; key `KosExtra_OBJ(row, 1)`.
- Call: `kos_item_load`.
- Settle: `RESULT_STEAL`.

#### FAST_ITEM_FAST

- Current: FAST_ITEM. Style EXTRA (FASTVAR obj, FASTVAR key), one
  cache slot. VAR twin VAR_ITEM_VAR, current VAR_ITEM.
- Stack: none.
- Fetch: obj shadow, key shadow.
- Call: `kos_item_load`.
- Settle: `RESULT_STEAL`.

### Stores, stack object

Stack order on entry is always obj, then key if on the stack, then
value if on the stack. The value is popped first; the tail pointer then
covers what remains.

#### ITEM_STORE

- Current: ITEM_STORE. Style NOARG, one cache slot.
- Stack: `[obj, key, val]`.
- Fetch: pop val; tail pointer of 2.
- Call: `kos_item_store(slot, obj, key, val)`.
- Settle: drop 2.

#### ITEM_STORE_CONST

- New. Style CONST (val), one cache slot.
- Stack: `[obj, key]`.
- Fetch: tail pointer of 2; val `KosOp_OBJ`, incref.
- Settle: drop 2.
- Squashed from `CONST v; ITEM_STORE`.

#### ITEM_STORE_FAST

- New. Style FASTVAR (val), one cache slot. VAR twin ITEM_STORE_VAR.
- Stack: `[obj, key]`.
- Fetch: val deref; tail pointer of 2.
- Settle: drop 2.
- Squashed from `VAR v; ITEM_STORE`.

#### ITEM_CONST_STORE

- Current: CONST_ITEM_STORE. Style CONST (key), one cache slot.
- Stack: `[obj, val]`.
- Fetch: pop val; tail pointer of 1; key `KosOp_OBJ`.
- Settle: drop 1.

#### ITEM_CONST_STORE_CONST

- New. Style EXTRA (CONST key, CONST val), one cache slot.
- Stack: `[obj]`.
- Fetch: tail pointer of 1; key and val from the row, val incref.
- Settle: drop 1.
- Squashed from `CONST v; ITEM_CONST_STORE`.

#### ITEM_CONST_STORE_FAST

- New. Style EXTRA (CONST key, FASTVAR val), one cache slot. VAR twin
  ITEM_CONST_STORE_VAR.
- Stack: `[obj]`.
- Fetch: val deref; tail pointer of 1; key from the row.
- Settle: drop 1.
- Squashed from `VAR v; ITEM_CONST_STORE`. Emitted for
  `(set! self.a["k"] v)`.

#### ITEM_FAST_STORE

- New. Style FASTVAR (key), one cache slot. VAR twin ITEM_VAR_STORE.
- Stack: `[obj, val]`.
- Fetch: key shadow; pop val; tail pointer of 1.
- Settle: release the key shadow; drop 1.
- The special emits ITEM_VAR_STORE when the object is an expression and
  the key a symbol, value on the stack.

#### ITEM_FAST_STORE_CONST

- New. Style EXTRA (FASTVAR key, CONST val), one cache slot. VAR twin
  ITEM_VAR_STORE_CONST.
- Stack: `[obj]`.
- Fetch: key shadow; tail pointer of 1; val from the row, incref.
- Settle: release the shadow; drop 1.
- Squashed from `CONST v; ITEM_VAR_STORE`. Emitted for
  `(set! grid[y][x] 0)`.

#### ITEM_FAST_STORE_FAST

- New. Style EXTRA (FASTVAR key, FASTVAR val), one cache slot. VAR twin
  ITEM_VAR_STORE_VAR.
- Stack: `[obj]`.
- Fetch: key shadow; val deref; tail pointer of 1.
- Settle: release the shadow; drop 1.
- Squashed from `VAR v; ITEM_VAR_STORE`. Emitted for
  `(set! self.cache[k] v)`.

### Stores, slot object

#### FAST_ITEM_STORE

- New. Style FASTVAR (obj), one cache slot. VAR twin VAR_ITEM_STORE.
- Stack: `[key, val]`.
- Fetch: obj shadow; pop val; tail pointer of 1 for the key.
- Settle: release the shadow; drop 1.
- The special emits VAR_ITEM_STORE when the object is a symbol and the
  key an expression. Today that shape pushes the object and runs
  ITEM_STORE.

#### FAST_ITEM_STORE_CONST

- New. Style EXTRA (FASTVAR obj, CONST val), one cache slot. VAR twin
  VAR_ITEM_STORE_CONST.
- Stack: `[key]`.
- Fetch: obj shadow; tail pointer of 1; val from the row, incref.
- Settle: release the shadow; drop 1.
- Squashed from `CONST v; VAR_ITEM_STORE`.

#### FAST_ITEM_STORE_FAST

- New. Style EXTRA (FASTVAR obj, FASTVAR val), one cache slot. VAR twin
  VAR_ITEM_STORE_VAR.
- Stack: `[key]`.
- Fetch: obj shadow; val deref; tail pointer of 1.
- Settle: release the shadow; drop 1.
- Squashed from `VAR v; VAR_ITEM_STORE`. Emitted for
  `(set! lst[(+ i 1)] v)`.

#### FAST_ITEM_CONST_STORE

- Current: FAST_CONST_ITEM_STORE. Style EXTRA (FASTVAR obj, CONST key),
  one cache slot. VAR twin VAR_ITEM_CONST_STORE, current
  VAR_CONST_ITEM_STORE.
- Stack: `[val]`.
- Fetch: obj shadow; pop val; key from the row.
- Settle: release the shadow. Nothing to drop.

#### FAST_ITEM_CONST_STORE_CONST

- Current: FAST_CONST_ITEM_STORE_CONST. Style EXTRA (FASTVAR obj, CONST
  key, CONST val), one cache slot. VAR twin VAR_ITEM_CONST_STORE_CONST,
  current VAR_CONST_ITEM_STORE_CONST.
- Stack: none.
- Fetch: obj shadow; key and val from the row, val incref.
- Settle: release the shadow.

#### FAST_ITEM_CONST_STORE_FAST

- New. Style EXTRA (FASTVAR obj, CONST key, FASTVAR val), one cache
  slot. VAR twin VAR_ITEM_CONST_STORE_VAR.
- Stack: none.
- Fetch: obj shadow; key from the row; val deref.
- Settle: release the shadow.
- Squashed from `VAR v; VAR_ITEM_CONST_STORE`. Emitted for
  `(set! x["k"] z)`.

#### FAST_ITEM_FAST_STORE

- Current: FAST_ITEM_STORE. Style EXTRA (FASTVAR obj, FASTVAR key), one
  cache slot. VAR twin VAR_ITEM_VAR_STORE, current VAR_ITEM_STORE.
- Stack: `[val]`.
- Fetch: obj shadow, key shadow; pop val.
- Settle: release both shadows.

#### FAST_ITEM_FAST_STORE_CONST

- Current: FAST_ITEM_STORE_CONST. Style EXTRA (FASTVAR obj, FASTVAR
  key, CONST val), one cache slot. VAR twin VAR_ITEM_VAR_STORE_CONST,
  current VAR_ITEM_STORE_CONST.
- Stack: none.
- Fetch: obj shadow, key shadow; val from the row, incref.
- Settle: release both shadows.

#### FAST_ITEM_FAST_STORE_FAST

- New. Style EXTRA (FASTVAR obj, FASTVAR key, FASTVAR val), one cache
  slot. VAR twin VAR_ITEM_VAR_STORE_VAR.
- Stack: none.
- Fetch: obj shadow, key shadow; val deref.
- Settle: release both shadows.
- Squashed from `VAR v; VAR_ITEM_VAR_STORE`. Emitted for
  `(set! x[y] z)`, the row this arc started from.

### Clear, take, and ref rows

No cache slot. Each verb has the six object and key cells of a load,
with the same fetch and release per origin, calling `kos_item_clear`,
`kos_item_take`, or `kos_item_ref`. CLEAR drops its stack operands and
pushes nothing (the special appends CONST None); TAKE and REF settle
their result as a load does: `RESULT_STEAL` with no stack operand,
`kos_item_replace_top` with one, `kos_stack_replace2_fast` with two.

| Verb | Row | Style | Stack | obj | key |
|---|---|---|---|---|---|
| CLEAR, TAKE, REF | ITEM_<verb> (current, unchanged) | NOARG | `[obj, key]` | tail 2 | tail 2 |
| | ITEM_CONST_<verb> (current CONST_ITEM_<verb>) | CONST | `[obj]` | tail 1 | row |
| | ITEM_FAST_<verb>, twin ITEM_VAR_<verb> (new) | FASTVAR | `[obj]` | tail 1 | shadow |
| | FAST_ITEM_<verb>, twin VAR_ITEM_<verb> (new) | FASTVAR | `[key]` | shadow | tail 1 |
| | FAST_ITEM_CONST_<verb>, twin VAR_ITEM_CONST_<verb> (new) | EXTRA (FASTVAR, CONST) | none | shadow | row |
| | FAST_ITEM_FAST_<verb>, twin VAR_ITEM_VAR_<verb> (new) | EXTRA (FASTVAR, FASTVAR) | none | shadow | shadow |

Twelve FAST-side rows and twelve VAR twins are new. The special forms
`compile/del-item` and `compile/take-item` emit the stack row and the
compress pipeline folds the operands, as for loads; `ref` rows are
emitted by the reference specials the same way.

### In-place rows

The operator field stays first on the row, ahead of the name-order
fields, as today: OPCOUNT (selector and count) when the operands are on
the stack, OPERAND (bare selector, count fixed at 1) when the operand is
the row's constant. Stack operands sit above the stack object and key.
A handler fetches obj and key, takes args as the tail pointer past the
stack object and key or as the address of the row's constant field,
calls `kos_item_inplace`, releases the shadows, drops the stack entries
it owns, and continues. The form's value is the CONST None the special
appends, unchanged.

Every example is `(inplace OP PLACE VALUE)`; `(incr! PLACE)` is the
same form with `+` and `1`.

#### ITEM_INPLACE

- Current: ITEM_INPLACE. Style OPCOUNT, one cache slot.
- Stack: `[obj, key, v0 .. v(count-1)]`.
- Fetch: tail pointer of count + 2; args is the tail past two.
- Settle: drop count + 2.

#### ITEM_INPLACE_CONST

- New. Style EXTRA (OPERAND, CONST rhs), one cache slot.
- Stack: `[obj, key]`.
- Fetch: tail pointer of 2; args is the address of the row's rhs field,
  count 1.
- Settle: drop 2.
- Squashed from `CONST v; ITEM_INPLACE` at count 1. Emitted for
  `(inplace + (f x)[(g i)] 1)`.

#### ITEM_CONST_INPLACE

- Current: CONST_ITEM_INPLACE. Style EXTRA (OPCOUNT, CONST key), one
  cache slot.
- Stack: `[obj, v0 .. v(count-1)]`.
- Fetch: tail pointer of count + 1; args is the tail past one; key from
  the row.
- Settle: drop count + 1.

#### ITEM_CONST_INPLACE_CONST

- New. Style EXTRA (OPERAND, CONST key, CONST rhs), one cache slot.
- Stack: `[obj]`.
- Fetch: tail pointer of 1; key and rhs from the row.
- Settle: drop 1.
- Squashed from `CONST v; ITEM_CONST_INPLACE` at count 1. Emitted for
  `(inplace + self.a["n"] 1)`.

#### ITEM_FAST_INPLACE

- New. Style EXTRA (OPCOUNT, FASTVAR key), one cache slot. VAR twin
  ITEM_VAR_INPLACE.
- Stack: `[obj, v0 .. v(count-1)]`.
- Fetch: key shadow; tail pointer of count + 1; args past one.
- Settle: release the shadow; drop count + 1.
- The special emits ITEM_VAR_INPLACE when the object is an expression
  and the key a symbol. Emitted for `(inplace + self.counts[k] v)`.

#### ITEM_FAST_INPLACE_CONST

- New. Style EXTRA (OPERAND, FASTVAR key, CONST rhs), one cache slot.
  VAR twin ITEM_VAR_INPLACE_CONST.
- Stack: `[obj]`.
- Fetch: key shadow; tail pointer of 1; rhs from the row.
- Settle: release the shadow; drop 1.
- Squashed from `CONST v; ITEM_VAR_INPLACE` at count 1. Emitted for
  `(incr! self.counts[k])`.

#### FAST_ITEM_INPLACE

- New. Style EXTRA (OPCOUNT, FASTVAR obj), one cache slot. VAR twin
  VAR_ITEM_INPLACE.
- Stack: `[key, v0 .. v(count-1)]`.
- Fetch: obj shadow; tail pointer of count + 1; key is the tail's first
  entry, args past one.
- Settle: release the shadow; drop count + 1.
- The special emits VAR_ITEM_INPLACE when the object is a symbol and
  the key an expression. Emitted for `(inplace + hist[(bucket x)] v)`.

#### FAST_ITEM_INPLACE_CONST

- New. Style EXTRA (OPERAND, FASTVAR obj, CONST rhs), one cache slot.
  VAR twin VAR_ITEM_INPLACE_CONST.
- Stack: `[key]`.
- Fetch: obj shadow; tail pointer of 1; rhs from the row.
- Settle: release the shadow; drop 1.
- Squashed from `CONST v; VAR_ITEM_INPLACE` at count 1. Emitted for
  `(incr! hist[(bucket x)])`.

#### FAST_ITEM_CONST_INPLACE

- Current: FAST_CONST_ITEM_INPLACE. Style EXTRA (OPCOUNT, FASTVAR obj,
  CONST key), one cache slot. VAR twin VAR_ITEM_CONST_INPLACE, current
  VAR_CONST_ITEM_INPLACE.
- Stack: `[v0 .. v(count-1)]`.
- Fetch: obj shadow; tail pointer of count is args; key from the row.
- Settle: release the shadow; drop count.

#### FAST_ITEM_CONST_INPLACE_CONST

- Current: FAST_CONST_ITEM_INPLACE_CONST. Style EXTRA (OPERAND, FASTVAR
  obj, CONST key, CONST rhs), one cache slot. VAR twin
  VAR_ITEM_CONST_INPLACE_CONST, current VAR_CONST_ITEM_INPLACE_CONST.
- Stack: none.
- Fetch: obj shadow; key and rhs from the row.
- Settle: release the shadow.

#### FAST_ITEM_FAST_INPLACE

- Current: FAST_ITEM_INPLACE. Style EXTRA (OPCOUNT, FASTVAR obj,
  FASTVAR key), one cache slot. VAR twin VAR_ITEM_VAR_INPLACE, current
  VAR_ITEM_INPLACE.
- Stack: `[v0 .. v(count-1)]`.
- Fetch: obj shadow, key shadow; tail pointer of count is args.
- Settle: release both shadows; drop count.

#### FAST_ITEM_FAST_INPLACE_CONST

- Current: FAST_ITEM_INPLACE_CONST. Style EXTRA (OPERAND, FASTVAR obj,
  FASTVAR key, CONST rhs), one cache slot. VAR twin
  VAR_ITEM_VAR_INPLACE_CONST, current VAR_ITEM_INPLACE_CONST.
- Stack: none.
- Fetch: obj shadow, key shadow; rhs from the row.
- Settle: release both shadows.

## Compiler

The compress pipeline builds the fused rows. A special emits the plain
stack sequence, obj, key, value or values, verb row, and the rules fold
the trailing run of feeders (bare VAR and CONST loads) into the row. A
special shapes its emission only where a non-feeder operand would sit
between a named operand and the row, which no adjacency rule can reach.

### Specials

Default emission, every verb: the object, the key, the value or values,
then the stack row. `compile/item`, `compile/del-item`,
`compile/take-item`, and the ref specials emit obj, key, row.
`compile/set-item` emits obj, key, val, ITEM_STORE. `compile/inplace-item`
emits obj, key, values, ITEM_INPLACE. `emit_item_key` no longer picks
the constant-key row; the rules do.

Shaped emission, when the default would strand a named operand behind a
non-feeder one:

- The object is a symbol and the key is not a feeder: the key and any
  values compile to the stack and the row names the object,
  `VAR_ITEM`, `VAR_ITEM_STORE`, `VAR_ITEM_INPLACE`, or the verb's row.
  Today this shape pushes the object.
- A value is not a feeder and the object is a symbol or the key is a
  feeder: the object compiles to the stack when compound, the values
  compile to the stack, and the row names what it can:
  `VAR_ITEM_VAR_STORE`, `VAR_ITEM_CONST_STORE`, `ITEM_VAR_STORE`,
  `ITEM_CONST_STORE`, and the in-place counterparts. Today's
  `emit_var_item_store` is this rule for a symbol object; it gains the
  compound-object case.

The condition in one line: emit plain when the key and every value are
feeders, else shape. A named key or object is requested from the
enclosing scope through `request_var`, as today.

### Fold rules

Rules are registered per trigger row, longest pattern first, in
`compile/processor.py`, one rule function over a table per verb. The
priors are the feeders directly ahead of the row in stack order, so a
matched run is always the trailing operands: value, then key, then
object.

Loads, and CLEAR, TAKE, and REF with the verb substituted:

| Priors | Result |
|---|---|
| VAR o, VAR k, ITEM | VAR_ITEM_VAR (o, k) |
| VAR o, CONST k, ITEM | VAR_ITEM_CONST (o, k) |
| VAR k, ITEM | ITEM_VAR (k) |
| CONST k, ITEM | ITEM_CONST (k) |

Stores, F standing for either feeder kind and the result naming its
kinds:

| Priors | Result |
|---|---|
| VAR o, F k, F v, ITEM_STORE | VAR_ITEM_{VAR,CONST}_STORE_{VAR,CONST} |
| F k, F v, ITEM_STORE | ITEM_{VAR,CONST}_STORE_{VAR,CONST} |
| F v, ITEM_STORE | ITEM_STORE_{VAR,CONST} |
| F v, VAR_ITEM_STORE | VAR_ITEM_STORE_{VAR,CONST} |

The last row folds the value into the shaped row for a symbol object
under a non-feeder key. The shaped rows with a named key never meet a
value feeder, since shaping happens only for a non-feeder value; the
value-fold table is still complete over every stack-value row so a
hand-assembled stream folds the same way.

In-place, count n with every value a feeder:

| Priors | Result |
|---|---|
| VAR o, F k, F x n, ITEM_INPLACE | the n feeders, then VAR_ITEM_{VAR,CONST}_INPLACE (o, k) |
| F k, F x n, ITEM_INPLACE | the n feeders, then ITEM_{VAR,CONST}_INPLACE (k) |
| CONST v, X_INPLACE at count 1 | X_INPLACE_CONST, for each of the six stack-operand rows |

The first two re-emit the value feeders behind the fused row, which
moves two pure loads past each other and nothing else. `pack_runs`
then packs the re-emitted run.

A compound object whose last instruction is a bare load matches as the
object feeder, so `(begin (f) x)[k]` folds x into the row with the
call left ahead of it. The load was the expression's last instruction
either way.

### Lowering

`build_var_to_fast` in `compile/processor.py` derives VAR twins from
FAST names by prefix, which fits the families whose names align. The
item rows do not align, so their map is written out: one table of the
38 VAR-to-FAST pairs of this family, merged into `VAR_TO_FAST` beside
the derived entries. The schema-driven lowering in `convert_fast`
already lowers each VAR field of an EXTRA row and the whole arg of a
plain VAR row, all or nothing, so no per-row lowering code is added.

`pack_runs` is unaffected.

## Serialization

Minor ABI bump in `serialize/constants.py`, with a note in the history
block: the item rows renamed and 36 inserted mid-enum, renumbering
every opcode after the VAR family. Slots are not serialized.

## Tests

- `tests/compile/specials/test_item_specials.py` and
  `test_inplace_specials.py`: the plain emission for every verb, and
  the two shaped emissions with the operand that triggers each.
- `tests/compile/test_compress.py` and `test_fuse_feeders.py`: every
  row of the fold tables, that a non-feeder between a named operand
  and the row leaves the run alone, the in-place fold at count 1 and
  its refusal above it, and the re-emitted value feeders.
- `tests/compile/test_fuse_feeders.py`: the written-out lowering map
  covers every VAR row of the family and lowers all or nothing.
- `tests/lib/opcode/test_item_fused.py`: hand-assembled rows for every
  new opcode, VAR twin included, over list, dict, tuple, and a protocol
  object, with NameError on an empty slot, a captured (ref-kind)
  operand, and a key whose hash rebinds the object's slot on the dict
  path.
- `tests/lib/opcode/test_item_electors.py`: election, miss, demotion,
  and re-election through the new slot kind, on the helpers' declared
  behavior only; the slot representation is internal.
- `tests/lib/opcode/test_item_inplace.py` and
  `test_item_inplace_electors.py`: every in-place row over int, float,
  and str cells, the write-in-place rule, counts above one, and the
  rhs guard on a stack operand whose type changes between executions.
- `tests/native/`: C-level cases for the helpers over hand-built
  frames, one per verb and origin pair, replacing whatever the current
  native item cases cover; then the ASan suite under KOS_NOFREELIST=1
  and valgrind.

## Documentation

- `design/OPCODES.md`: rename the sections, add 60, and state the
  three-position grammar in the family prose.
- `docs/reference/vm/opcodes/`: `git mv` the 26 renamed pages, add 60.
- `docs/reference/vm/families.md`, `docs/reference/vm/index.md`: the
  tables and sections.
- `docs/reference/specials/compile-item.md`, `compile-set-item.md`,
  `compile-inplace-item.md`, `compile-del-item.md`,
  `compile-take-item.md`: the row names, the plain emission, and the
  two shaped cases.
- `design/interpreter/electors.md`, `bytecode.md`,
  `resumable-reads.md`, `design/api/NAMESPACE-AND-REFS.md`,
  `design/SPECIALS.md`: the row names where cited.
- `design/archive/` and `design/trace/`: historical, left as written.

## Touch list

| Area | Change |
|---|---|
| `koskari/lib/opcodes.incl` | rename 26 rows, insert 60 |
| `koskari/lib/handlers.incl` | regenerate |
| `koskari/lib/opcode/` | `git mv` 26 files, add 60, rewrite every item handler to the fetch, call, settle shape |
| `koskari/lib/eval/eval_item.c` | new: the six helpers, the load and store bodies, the in-place body tables, slot helpers |
| `koskari/lib/eval/eval_electors.c` | remove the item load, store, and in-place families, their fetch, value, and body macros, classify functions, elect rows, and `kos_elector_generic_of` cases; `kos_item_inplace_cell`, `kos_item_inplace_store`, `kos_item_inplace_release`, and `kos_item_inplace_reduce` move to `eval_item.c` |
| `koskari/lib/bytecode.h` and the slot teardown switch | `KOS_CACHE_ITEM` kind and payload; no-op teardown arm |
| `koskari/compile/specials/items.py` | plain emission and the two shaped cases for item, set-item, del-item, take-item |
| `koskari/compile/specials/object.py` | the same for `compile/inplace-item` |
| the ref specials | plain emission of ITEM_REF |
| `koskari/compile/processor.py` | fold tables, written-out lowering map |
| `koskari/serialize/constants.py` | ABI minor |
| tests, docs, design | as above |

## Decisions recorded

- The in-place rows are in this arc, on `kos_item_inplace`, with their
  grid filled. Leaving them on the elector macros would have kept two
  handler shapes in one family.
- `KOS_CACHE_ITEM` is a new cache kind. Reusing `KOS_CACHE_ELECTOR`
  with the body cast to and from `KosThreadFn` would hide the signature
  change from the compiler to save three small helpers.
- The compress pipeline builds the fused rows from plain emission. A
  special shapes its emission only where a non-feeder operand would
  strand a named operand behind it, which no adjacency rule reaches.
- CLEAR, TAKE, and REF take the load grid. They elect nothing, and
  their helpers exist for the hold across the protocol call.
- Object origins are stack and slot. An attribute or other compound
  object is a stack object one row ahead; an attr origin would be a
  fourth axis for one row saved.
- Bodies are function pointers in the slot, not an enum switched
  inline, for the code growth reason above.
- Only the elected hit path of a helper is inline.

# The end.
