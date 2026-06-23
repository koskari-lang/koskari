# KosNamespace Fast-Slot Layout and Hot-Path Guard Removal

This document describes two closely related optimizations to the fast-local
variable access path:

1. **Restructure `KosNameBind`** — separate the per-slot `raw` flag and symbol
   `name` from the hot `held` pointer so that the slot array fits more tightly
   in L1 cache.
2. **Remove `KosNamespace_SupportsFast` from the opcode hot path** — the type
   check is redundant for correctly compiled code and should become a
   debug-only assertion.

Together these address the dominant cost in `FAST` (21.7% of total runtime,
1.56 billion executions) and `FAST_STORE` (4.6%, 289 million executions), as
measured under `KOS_TRACE_TIMING=1` on a computation-heavy workload.

**See also:**
[freeze-thaw-context-segments.md](freeze-thaw-context-segments.md) (C-local
eval state; broader execution fast-path);
[opcode-eval-helpers-inventory.md](opcode-eval-helpers-inventory.md) (opcode
helper survey); [EVALUATOR.md](../EVALUATOR.md) §8 (native handler protocol).


---


## 1. Background: how FAST works today

`FAST` loads a value from a numbered slot in the current namespace and pushes
it onto the operand stack.  Its execution path is:

```
FAST opcode handler
  └─ CURRENT_NS()                         — load es->ns
  └─ KosNamespace_SupportsFast(ns)        — type check (2 loads + 2 cmp + OR)
  └─ KosNamespace_FastDeref(ns, idx)      — C function call
       ├─ KosNamespace_FAST_COUNT(ns)     — bounds check
       ├─ KosNamespace_GET_FAST(ns, i)    — compute &ns->fast[i]
       ├─ KosNameBind_RAW(nb)             — load raw flag
       ├─ KosNameBind_HELD(nb)            — load value pointer
       ├─ Py_INCREF(value)                — bump refcount
       └─ return value
  └─ kos_op_result_PUSH(es, val)         — push (inline)
```

`FAST_STORE` mirrors this: it pops TOS, does the same guard checks, then
writes `held` (or calls `KosRef_Store` for ref-cell locals).  All
`FAST_OBJECT_*` fused opcodes (e.g. `FAST_OBJECT_BIT_XOR`, 389 million
executions) perform the same `SupportsFast` check and `FastDeref` call for
the fast-local operand.


---


## 2. The `KosNameBind` layout problem

### 2.1 Current struct

```c
typedef struct KosNameBind {
  PyObject *name;   /* symbol — used for NameError messages only */
  PyObject *held;   /* raw=1: value directly; raw=0: KosRef object */
  int raw;          /* 0 or 1 */
  /* 4 bytes implicit padding */
} KosNameBind;     /* sizeof == 24 bytes */
```

### 2.2 `KosNamespace` memory layout

```c
typedef struct KosNamespace {
  PyObject_VAR_HEAD        /* ob_refcnt, ob_type, ob_size — 24 bytes */
  PyObject *name;          /* offset 24 */
  PyObject *parent;        /* offset 32 */
  PyObject *binds;         /* offset 40 */
  size_t    fast_count;    /* offset 48 */
  KosNameBind fast[0];     /* offset 56 — variable-length array */
} KosNamespace;
```

Fast slots start at offset 56.  With `sizeof(KosNameBind) == 24`:

| Slot | `name` field | `held` field | `raw` field |
|------|-------------|-------------|------------|
| 0    | bytes 56–63 | bytes 64–71 | bytes 72–75 |
| 1    | bytes 80–87 | bytes 88–95 | bytes 96–99 |
| 2    | bytes 104–111 | bytes 112–119 | bytes 120–123 |
| 3    | bytes 128–135 | bytes 136–143 | bytes 144–147 |

Cache lines are 64 bytes.  A function with 4 fast locals touches:

- **Line 0** (bytes 0–63): namespace header fields + `fast[0].name`
- **Line 1** (bytes 64–127): `fast[0].held`, `fast[0].raw`, `fast[1].name`,
  `fast[1].held`, `fast[1].raw`, `fast[2].name`
- **Line 2** (bytes 128–191): `fast[2].held`, `fast[2].raw`, `fast[3].name`,
  `fast[3].held`, `fast[3].raw`

The `name` field is needed only on the error path (`NameError`).  On the
common path it is pure waste: it displaces useful data to adjacent cache
lines, meaning a 4-slot function touches 3 cache lines where 1–2 would
suffice.

### 2.3 Quantified cache pressure

At 20 ns average for FAST, with 28 ns as the overall mean dispatch time, the
opcode is already below mean — but it accounts for 21.7% of total time because
of its sheer execution count.  A typical L1 cache hit is 1–4 ns; an L2 hit is
5–12 ns.  Reducing the working set of the slot array from 3 cache lines to 1
for a 4-slot function is a plausible 3–6 ns saving per FAST, or roughly
4–9 seconds saved on a 1.56-billion-FAST workload.

`FAST_STORE` (289 million executions) and all `FAST_OBJECT_*` fused opcodes
benefit in proportion.


---


## 3. Proposed change A: restructure the fast-slot array

### 3.1 Design options considered

**Option 1 — Shrink `KosNameBind` to 16 bytes** by moving `name` to a
separate parallel cold array:

```c
typedef struct KosNameBind {
  PyObject *held;   /* offset 0 */
  int raw;          /* offset 8 */
  int _pad;         /* offset 12 */
} KosNameBind;     /* sizeof == 16 bytes */
```

A parallel `PyObject **fast_names` pointer (or a `PyObject *fast_names[0]`
trailing array) holds the name for error messages.  3 slots per cache line
instead of ~2.5.  Straightforward but `raw` still costs a load and branch.

**Option 2 — Make the hot array just `PyObject *` with a bitmap for `raw`**
(recommended):

```c
/* In KosNamespace: */
uint64_t fast_raw_bitmap;  /* bit i = 1 means fast[i] is raw (direct value) */
PyObject *fast[0];         /* slot i: raw? value directly : KosRef object */
```

`KosNameBind` is eliminated entirely.  `fast[i]` is a plain `PyObject *`
(8 bytes per slot, 8 slots per 64-byte cache line).  The `raw` test becomes
`(ns->fast_raw_bitmap >> idx) & 1` — a single shift and mask on values that
are almost always already in a register.

Cold data (names for error messages) moves to a separate `PyObject
**fast_names` array that is only allocated and consulted when a `NameError`
must be raised.

### 3.2 Recommended design: plain pointer array + `fast_raw_bitmap`

#### Namespace struct changes

```c
typedef struct KosNamespace {
  PyObject_VAR_HEAD        /* ob_refcnt, ob_type, ob_size — 24 bytes */
  PyObject *name;          /* offset 24 — namespace symbol */
  PyObject *parent;        /* offset 32 */
  PyObject *binds;         /* offset 40 */
  size_t    fast_count;    /* offset 48 */
  uint64_t  fast_raw_bitmap; /* offset 56 — bit i: fast[i] is a raw value */
  PyObject *fast_names;    /* offset 64 — PyTupleObject of per-slot symbols,
                              or NULL if not yet materialized */
  PyObject *fast[0];       /* offset 72 — hot slot array */
} KosNamespace;
```

Fast slots start at offset 72.  With 8 bytes per slot, 8 slots fit in one
64-byte cache line.  A function with 8 locals touches only 1 cache line for
all slot data (assuming `fast_count` and `fast_raw_bitmap` are in the
preceding line, which they will be for warm namespaces).

#### Updated accessor macros

```c
#define KosNamespace_FAST_COUNT(ns)       (((KosNamespace *)(ns))->fast_count)
#define KosNamespace_FAST_RAW_BITMAP(ns)  (((KosNamespace *)(ns))->fast_raw_bitmap)
#define KosNamespace_FAST_NAMES(ns)       (((KosNamespace *)(ns))->fast_names)
#define KosNamespace_GET_FAST_VAL(ns, i)  (((KosNamespace *)(ns))->fast[(i)])
#define KosNamespace_FAST_IS_RAW(ns, i)   \
    (((KosNamespace_FAST_RAW_BITMAP(ns)) >> (i)) & 1)
```

#### FastDeref — new hot path

```c
/* Inline candidate; replaces KosNamespace_FastDeref for hot FAST opcode. */
static inline PyObject *
kos_namespace_fast_deref_inline(PyObject *ns, size_t idx) {
  /* bounds check omitted in release builds — see §4 */
  if (KosNamespace_FAST_IS_RAW(ns, idx)) {
    PyObject *val = KosNamespace_GET_FAST_VAL(ns, idx);
    if (unlikely(! val)) {
      kos_namespace_raise_unbound(ns, idx);  /* slow path: materialize name */
      return NULL;
    }
    Py_INCREF(val);
    return val;
  }
  return KosRef_Deref(KosNamespace_GET_FAST_VAL(ns, idx));
}
```

The common case (raw slot, non-NULL) is: one bitmap load (likely cached),
shift+mask, one slot load, one incref, return.  No function call.

#### FastStore — new hot path

```c
static inline int
kos_namespace_fast_store_inline(PyObject *ns, size_t idx, PyObject *val) {
  if (KosNamespace_FAST_IS_RAW(ns, idx)) {
    Py_INCREF(val);
    Py_XDECREF(KosNamespace_GET_FAST_VAL(ns, idx));
    KosNamespace_GET_FAST_VAL(ns, idx) = val;
    return 0;
  }
  return KosRef_Store(KosNamespace_GET_FAST_VAL(ns, idx), val);
}
```

#### Bitmap constraint

`uint64_t fast_raw_bitmap` supports up to 64 fast slots.  The compiler must
assert or enforce `fast_count <= 64` when building namespaces; if a function
ever exceeds this limit (unlikely in practice), the namespace builder must
fall back to the slow `KosNameBind` representation or the limit must be
raised to a wider bitmap or a pointer to a heap bitmap.  In practice, Koskari
functions rarely approach 64 named locals; the limit is not expected to bind.

#### Error path: `fast_names`

On a `NameError` (slot is NULL or ref-cell yields NULL), the error reporter
calls a slow helper that materializes `fast_names` (a Python tuple of symbols,
allocated once on first error and cached on the namespace).  This keeps the
symbol pointers entirely off the hot path.

```c
static void kos_namespace_raise_unbound(PyObject *ns, size_t idx) {
  PyObject *names = KosNamespace_FAST_NAMES(ns);
  if (! names) {
    names = kos_namespace_build_fast_names(ns); /* consults binds dict */
    KosNamespace_FAST_NAMES(ns) = names;        /* cache for next error */
  }
  if (names && idx < (size_t) PyTuple_GET_SIZE(names)) {
    PyErr_Format(PyExc_NameError, "Unbound name: %S",
                 PyTuple_GET_ITEM(names, idx));
  } else {
    PyErr_Format(PyExc_NameError, "Unbound fast slot %zu", idx);
  }
}
```

### 3.3 Migration of ref-cell slots

Currently `raw=0` means the slot holds a `KosRef` object; `FAST` calls
`KosRef_Deref(nb->held)`.  Under the new layout the same logic applies:

```c
if (! KosNamespace_FAST_IS_RAW(ns, idx))
  return KosRef_Deref(KosNamespace_GET_FAST_VAL(ns, idx));
```

The bitmap bit for a ref-cell slot is 0; the bitmap is initialized at
namespace construction in `namespace.c` and mutated via
`KosNamespace_ConvertToRef` (currently `KosNameBind_RAW(nb) = 0`).

### 3.4 `KosCollectorNamespace`

`KosCollectorNamespace` inherits from `KosNamespace` and uses the same fast
array.  Its `fast[0]` starts at the same offset within its `super` field.
Both the struct change and the accessor macros apply uniformly; the collector
subtype's own code only needs to update any direct `KosNameBind`-typed uses.


---


## 4. Proposed change B: `KosNamespace_SupportsFast` → debug-only assertion

### 4.1 Current behavior

Every FAST, FAST\_STORE, CALL\_FAST, FAST\_OBJECT\_\*, SWITCH\_EQ\_FAST, and
UNPACK\_FAST opcode begins with:

```c
if (! KosNamespace_SupportsFast(ns)) {
  PyErr_Format(PyExc_RuntimeError, "FAST invoked on %R", ns);
  return EVAL_RAISE();
}
```

`KosNamespace_SupportsFast` expands to a two-branch type check:

```c
(ns->ob_type == &KosNamespaceType || ns->ob_type == &KosCollectorNamespaceType)
```

This is two pointer loads and two comparisons per dispatch, on every one of
the ~33 opcode variants that use fast locals.

### 4.2 Why the check is redundant

The compiler (`koskari/compile/compiler.py`) emits FAST and FAST\_STORE only
inside function bodies whose namespace is a `KosNamespace`.  The namespace
type is established at frame push time and cannot change during evaluation of
that frame.  A non-fast namespace reaching a FAST opcode indicates a compiler
bug, not a recoverable runtime condition.

### 4.3 Proposed replacement

In release builds: remove the check entirely from all hot-path opcode handlers.

In debug builds (`KOS_DEBUG` or `VERBOSE_DEBUG`): replace with an assertion
macro that fires before opcode dispatch — this keeps the protection during
development at zero production cost.

```c
/* Replaces the runtime check in each opcode handler: */
KOS_ASSERT(KosNamespace_SupportsFast(ns),
           "FAST invoked on non-fast namespace — compiler bug");
```

The 32+ opcode files that contain this guard can be updated mechanically.
Opcode handlers that are already unreachable on the `!SupportsFast` path (any
opcode that immediately precedes `KosNamespace_FastDeref`) should have the
guard removed entirely.

### 4.4 Affected opcode files (complete list)

All files under `koskari/lib/opcode/` that call `KosNamespace_SupportsFast`:

`FAST.c`, `FAST_STORE.c`, `FAST_STORE_CONST.c`, `FAST_CLEAR.c`,
`FAST_REF.c`, `FAST_BIND.c`, `FAST_ATTR.c`, `FAST_ATTR_STORE.c`,
`FAST_ATTR_CLEAR.c`, `FAST_ATTR_REF.c`, `FAST_OBJECT_ADD.c`,
`FAST_OBJECT_BIT_AND.c`, `FAST_OBJECT_BIT_LEFT.c`, `FAST_OBJECT_BIT_OR.c`,
`FAST_OBJECT_BIT_RIGHT.c`, `FAST_OBJECT_BIT_XOR.c`,
`FAST_OBJECT_FLOOR_DIVIDE.c`, `FAST_OBJECT_MATRIX_MULTIPLY.c`,
`FAST_OBJECT_MODULO.c`, `FAST_OBJECT_MULTIPLY.c`, `FAST_OBJECT_POWER.c`,
`FAST_OBJECT_SUBTRACT.c`, `FAST_OBJECT_TRUE_DIVIDE.c`,
`CALL_FAST.c`, `CALL_FAST_NAMES.c`, `CALL_FAST_STAR.c`,
`CALL_FAST_ATTR.c`, `CALL_FAST_ATTR_NAMES.c`, `CALL_FAST_ATTR_STAR.c`,
`SWITCH_EQ_FAST.c`, `SWITCH_EQ_FAST_ATTR.c`, `UNPACK_FAST.c`.


---


## 5. Additional hot-path opportunities (bonus scope)

These are smaller wins that fit naturally into the same branch:

### 5.1 Inline `KosNamespace_FastDeref` into the FAST opcode

With `KosNameBind` gone and the bitmap check reduced to a register operation,
`kos_namespace_fast_deref_inline` (§3.2) is small enough to inline directly
into the FAST opcode handler — eliminating the `KosNamespace_FastDeref`
function call entirely.  Same for FAST\_STORE.  The function-call forms remain
as non-inline entry points for callers outside the opcode dispatch path.

### 5.2 Bounds check in release builds

`KosNamespace_FastDeref` currently does a bounds check (`index >=
fast_count`) that raises `NameError` with a formatted message.  Like the type
guard, this defends against a compiler bug, not user error.  In release builds
it can become a debug-only assertion; the slot array is sized at namespace
construction time to match the code object's fast-local count.

### 5.3 `KosNamespace_FastDeref` public API

The public `KosNamespace_FastDeref` function in `namespace.h` is called from
`formals.c` and potentially from outside the opcode path.  It must retain its
bounds check and error behavior.  Only the inline hot-path variant (§3.2)
drops the check.


---


## 6. Implementation plan

Work is organized into four phases.  Each phase is independently testable.

### Phase 1 — `namespace.h` / `namespace.c`: restructure the slot type

1. Remove `KosNameBind` struct and all `KosNameBind_*` macros.
2. Add `fast_raw_bitmap` (`uint64_t`) and `fast_names` (`PyObject *`) fields
   to `KosNamespace`.
3. Replace `fast[0]` element type from `KosNameBind` to `PyObject *`.
4. Update `KosNamespace_GET_FAST`, `KosNamespace_FAST_COUNT` macros; add
   `KosNamespace_GET_FAST_VAL`, `KosNamespace_FAST_IS_RAW`,
   `KosNamespace_FAST_RAW_BITMAP`, `KosNamespace_FAST_NAMES` macros.
5. Update `KosNamespace_New` / `KosNamespace_NewFull`: initialize
   `fast_raw_bitmap = 0` (all raw by default); slots initialized to `NULL`.
6. Update `KosNamespace_FastDeref`, `KosNamespace_FastStore`,
   `KosNamespace_FastClear`, `KosNamespace_FastRef`,
   `KosNamespace_ConvertToRef` (sets bitmap bit 0 for slot, puts ref into
   slot), and the copy logic in `KosNamespace_NewFromTemplate`.
7. Add `kos_namespace_raise_unbound` (error-path helper) and
   `kos_namespace_build_fast_names` (lazy name-tuple materializer).
8. Update `formals.c` (calls `KosNameBind` APIs during formals installation).
9. `make test` — must pass all phases.

### Phase 2 — inline deref/store helpers

1. Add `kos_namespace_fast_deref_inline` and
   `kos_namespace_fast_store_inline` as `static inline` functions in
   `namespace.h` (or in an `eval`-internal header if preferred).
2. Wire FAST and FAST\_STORE to the inline variants as a proof of concept.
3. `make test`.

### Phase 3 — FAST\_OBJECT\_\* and CALL\_FAST\* opcodes: inline + guard removal

1. For each opcode in the list in §4.4: replace `KosNamespace_SupportsFast`
   check with debug assertion; wire to inline deref helper.
2. For `FAST_OBJECT_*` fused opcodes the fast-local load is already a single
   site; the inline should produce a net reduction of one function call and
   one type check per dispatch.
3. `make test`.

### Phase 4 — remaining FAST\_\* opcodes

`FAST_CLEAR`, `FAST_REF`, `FAST_BIND`, `FAST_ATTR*`, `SWITCH_EQ_FAST*`,
`UNPACK_FAST` — guard removal and inline wiring as appropriate.  Some of these
have more complex surrounding logic; do them last to keep diffs reviewable.

`make test`.


---


## 7. Risks and tradeoffs

| Risk | Notes |
|---|---|
| `fast_count > 64` | Bitmap is `uint64_t`; limit is 64 slots. Compiler should assert. In practice Koskari functions have O(10) locals. Can widen to heap bitmap if ever needed. |
| `fast_names` lazy materialization | First `NameError` in a session allocates a tuple per namespace. Acceptable; errors are not performance-critical. |
| `KosCollectorNamespace` | Uses same fast array; must apply same struct change. Extra care on its `copy` / `freeze` paths. |
| `formals.c` initialization | Currently iterates `KosNameBind` structs. Phase 1 must update this; it is the only non-opcode C caller of slot internals. |
| Serialization (`.kio`) | If any serializer reads `KosNameBind` fields directly (rather than via accessors), it must be updated. Check `koskari/serialize/`. |
| Python-side namespace introspection | `namespace.c` exposes namespace contents via `__iter__`, `__getitem__`, etc. These go through `binds` dict, not fast slots directly — unaffected. |


---


## 8. Expected impact

Based on the `KOS_TRACE_TIMING` trace (5.1 billion opcode dispatches,
~145 s total opcode time):

| Optimization | Affected executions | Estimated saving |
|---|---|---|
| `KosNameBind` layout (cache density) | 1.56B FAST + 289M FAST_STORE + 389M FAST_OBJECT_BIT_XOR + … ≈ 2.8B | 3–6 ns/dispatch → **8–17 s** |
| `SupportsFast` guard removal | same 2.8B dispatches | 1–2 ns/dispatch → **3–6 s** |
| Inline `FastDeref` / `FastStore` | 1.56B FAST + 289M FAST_STORE | 2–4 ns/dispatch → **4–7 s** |

These ranges are estimates; actual savings depend on the cache behavior of
the specific workload.  The layout change is the highest-confidence item
because the cache-line analysis is mechanical; the guard and inline savings
depend on compiler output.


---

# The end.
