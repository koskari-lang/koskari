# KosNamespace Freelist and Oversize Allocation

This document describes the freelist for `KosNamespace` objects, combined with
static oversize allocation to a minimum slot count, so that call-site
namespaces fit a single pool regardless of their exact local-variable count.

**Context:**
[namespace-fast-slot-layout.md](../archive/namespace-fast-slot-layout.md) (prior
restructuring; current slot layout); [EVALUATOR.md](../EVALUATOR.md) §8;
[api/tracing.md](../api/tracing.md) (freelist tracing infrastructure).


---


## 1. Background: the missing freelist

Every Koskari function call allocates a fresh `KosNamespace` via
`PyObject_NewVar`, which ultimately calls `malloc`.  On the hot call path
(`CALL_FAST` → `KosEvalState_Call` → `func_obtain_call_namespace` →
`KosNamespace_Copy`), this allocation is the dominant fixed cost that cannot
be amortized:

- `PyDict_New` for the `binds` dict is already CPython-freelisted; it is
  cheap.
- Per-closure-slot work (2× `Py_INCREF` + `PyDict_SetItem`) is proportional
  to slot count and unavoidable.
- `PyObject_NewVar` itself is a `malloc` call.  There is no existing freelist
  for `KosNamespace`.

A `KOS_TRACE_TIMING` trace of a computation-heavy workload shows `CALL_FAST`
at **76 million invocations**, **~123 ns average**.  About 58 ns of that is
`retarget_after_push` (sync of live eval state after frame push) — a separate
bottleneck.  The remaining ~65 ns is dominated by the `PyObject_NewVar` path
in `KosNamespace_Copy`.

By contrast, the `dyn_node` and `stack_block` freelists each hit **76 million
allocations** with fewer than 20 misses — near-100 % hit rates.  A namespace
freelist should achieve comparable efficiency once the pool is primed.


---


## 2. `ob_size` / `fast_count` split: why oversize allocation is safe

`KosNamespace` derives from `PyObject_VAR_HEAD`:

```c
typedef struct KosNamespace {
  PyObject_VAR_HEAD    /* ob_refcnt, ob_type, ob_size — allocation capacity */
  /* ... header fields ... */
  size_t  fast_count;  /* logical slot count — always ≤ ob_size */
  /* ... */
  PyObject *fast[0];   /* variable-length array, ob_size slots allocated */
} KosNamespace;
```

`ob_size` (via `Py_SIZE`) records how many `PyObject *` elements were
allocated in the flexible array.  `fast_count` records how many of those slots
the namespace actually uses.  Every consumer (`ns_dealloc`, `KosNamespace_Copy`,
`ns_newfull_from_nspec`, all opcode handlers) iterates up to `fast_count` — no
code reads or writes beyond that index.

This means an object allocated with `ob_size = KOS_NS_MIN_SLOTS` but
`fast_count = N ≤ KOS_NS_MIN_SLOTS` behaves identically to one allocated with
exactly N slots.  The extra `KOS_NS_MIN_SLOTS - N` tail slots exist in memory
but are never touched by the VM.


---


## 3. Proposed design

### 3.1 New compile-time constants

```c
/* namespace.h (or namespace.c) */

/* Minimum (and standard) allocation size for freelisted namespaces.
   All functions with ≤ KOS_NS_MIN_SLOTS locals+closures+globals share one
   pool.  Adjust based on trace hit/miss data.  Default 32. */
#ifndef KOS_NS_MIN_SLOTS
#define KOS_NS_MIN_SLOTS 32
#endif

/* Maximum depth of the namespace freelist. */
#define KOS_NS_FREELIST_MAX 256
```

### 3.2 Freelist storage

```c
/* namespace.c — file scope */
static PyObject *ns_freelist[KOS_NS_FREELIST_MAX];
static size_t    ns_freecount = 0;
```

### 3.3 `ns_alloc` — allocation helper

A new internal function replaces direct `PyObject_NewVar` calls in
`KosNamespace_Copy` and `ns_newfull_from_nspec`:

```c
static PyObject *ns_alloc(PyTypeObject *type, size_t count) {
  if (type == &KosNamespaceType &&
      count <= KOS_NS_MIN_SLOTS &&
      ns_freecount > 0) {

    PyObject *p = ns_freelist[--ns_freecount];
    KOS_TRACE_FL_ALLOC("namespace", 1);

    /* Restore PyObject header. fast[] and header fields are already
       zeroed by ns_dealloc before the slab was cached. */
    Py_SET_REFCNT(p, 1);
    Py_SET_TYPE(p, type);
    Py_SET_SIZE(p, (Py_ssize_t) KOS_NS_MIN_SLOTS);
    return p;
  }

  KOS_TRACE_FL_ALLOC("namespace", 0);
  size_t alloc = (count < (size_t) KOS_NS_MIN_SLOTS)
                 ? (size_t) KOS_NS_MIN_SLOTS : count;
  PyObject *p = PyObject_NewVar(PyObject, type, (Py_ssize_t) alloc);
  if (! p)
    return NULL;

  /* PyObject_NewVar does not zero the flexible array.  Zero ALL alloc slots
     so that tail slots (above count) are clean for future recycles. */
  memset(((KosNamespace *) p)->fast, 0, alloc * sizeof(PyObject *));
  return p;
}
```

Callers are responsible for initializing all fixed header fields
(`name`, `parent`, `binds`, `fast_count`, `fast_names`, `fast_raw_lo`,
`fast_raw_hi`) before use, as they do today.

### 3.4 `ns_dealloc` — return to freelist

```c
static void ns_dealloc(PyObject *self) {
  Py_CLEAR(KosNamespace_NAME(self));
  Py_CLEAR(KosNamespace_PARENT(self));
  Py_CLEAR(KosNamespace_BINDS(self));
  Py_CLEAR(KosNamespace_FAST_NAMES(self));

  for (size_t i = KosNamespace_FAST_COUNT(self); i--; )
    Py_CLEAR(KosNamespace_GET_FAST_VAL(self, i));
  KosNamespace_FAST_COUNT(self) = 0;

  /* After the loop, fast[0..fast_count-1] are NULL; fast[fast_count..ob_size-1]
     have been NULL since initial alloc (zeroed in ns_alloc, never written). */
  if (Py_TYPE(self) == &KosNamespaceType &&
      Py_SIZE(self) == (Py_ssize_t) KOS_NS_MIN_SLOTS &&
      ns_freecount < KOS_NS_FREELIST_MAX) {

    /* Zero remaining header fields so ns_alloc returns a clean slab. */
    KosNamespace_FAST_RAW_LO(self) = 0;
    KosNamespace_FAST_RAW_HI(self) = 0;
    /* name, parent, binds, fast_names, fast_count already zero (Py_CLEAR
       above or the loop termination). */
    KOS_TRACE_FL_RETURN("namespace", 0);
    ns_freelist[ns_freecount++] = self;
    return;
  }

  KOS_TRACE_FL_RETURN("namespace", 1);
  Py_TYPE(self)->tp_free(self);
}
```

### 3.5 Module teardown — drain the freelist

`koskari_namespace_clear` must drain the freelist before the extension unloads.
Leaving freelisted `PyObject *` alive after `KosNamespaceType` is torn down
would produce a use-after-free when CPython's cleanup eventually processes them.

```c
int koskari_namespace_clear(PyObject *mod) {
  while (ns_freecount > 0) {
    PyObject *p = ns_freelist[--ns_freecount];
    PyObject_Free(p);  /* bypass tp_free; type may already be gone */
  }

  Py_CLEAR(_namespace_str);
  Py_CLEAR(_module_str);
  return 0;
}
```


---


## 4. Zeroing invariant

The correctness of freelist reuse depends on a simple invariant: all
`KOS_NS_MIN_SLOTS` fast slots are NULL when a slab is in the freelist.

**On fresh allocation** (`ns_alloc`, miss path): `PyObject_NewVar` does not
zero the flexible array.  `ns_alloc` explicitly `memset`s all `alloc` slots to
zero after allocation.  For the standard case (`count ≤ KOS_NS_MIN_SLOTS`),
`alloc == KOS_NS_MIN_SLOTS`, so the full tail is zeroed.

**On deallocation** (`ns_dealloc`, hit path): the existing loop clears
`fast[0..fast_count-1]` via `Py_CLEAR`.  Slots `fast[fast_count..ob_size-1]`
have been NULL since fresh allocation and are never written by the VM (the VM
only writes into `0..fast_count-1`).  After the loop, all `KOS_NS_MIN_SLOTS`
slots are provably NULL; no memset is needed.

**Summary**:

| Path | Slots 0..count-1 | Slots count..MIN_SLOTS-1 |
|---|---|---|
| Fresh alloc | zeroed by `ns_alloc` memset | zeroed by `ns_alloc` memset |
| After `ns_dealloc` | zeroed by `Py_CLEAR` loop | zeroed since fresh alloc; never touched |
| On freelist pop | zeroed (from dealloc) | zeroed (from fresh alloc or prior dealloc) |


---


## 5. Tracing hooks

The new freelist participates in the existing `KOS_TRACE_FREELIST`
infrastructure identically to `dyn_node` and `stack_block`.

### 5.1 Call sites

`KOS_TRACE_FL_ALLOC("namespace", 1)` — freelist hit in `ns_alloc`.
`KOS_TRACE_FL_ALLOC("namespace", 0)` — fresh `PyObject_NewVar` in `ns_alloc`.
`KOS_TRACE_FL_RETURN("namespace", 0)` — slab cached in `ns_dealloc`.
`KOS_TRACE_FL_RETURN("namespace", 1)` — overrun (count > MIN or freelist full)
in `ns_dealloc`.

These four call sites are the only changes to `namespace.c` for tracing.
`namespace.c` must `#include "tracing.h"` (it does not currently).

### 5.2 `tracing.c` additions

Add a `"namespace"` counter alongside the existing freelists:

```c
#define KOS_FL_NAME_NAMESPACE "namespace"
static kos_fl_counters_t fl_namespace = {0, 0, 0, 0};
```

Add a branch in `kos_trace_fl_lookup`:

```c
if (strcmp(name, KOS_FL_NAME_NAMESPACE) == 0)
  return &fl_namespace;
```

Add to `kos_trace_init` / `kos_trace_clear`:

```c
memset(&fl_namespace, 0, sizeof(fl_namespace));
```

Add to `add_fl_section` (`ADD_FL_ENTRY`):

```c
ADD_FL_ENTRY(KOS_FL_NAME_NAMESPACE, fl_namespace);
```

The JSON output will then include a `"namespace"` key in the `"freelists"`
section, with `alloc_hits`, `alloc_misses`, `return_hits`, and `overruns`.

### 5.3 Interpreting the trace

A healthy freelist shows `alloc_hits` ≈ `return_hits` ≈ call count, with
`alloc_misses` near zero (only the first few calls before the pool is primed).

**High `alloc_misses`** relative to `alloc_hits` means `KOS_NS_MIN_SLOTS` is
too small — most functions in the workload exceed the minimum.  Raise the
constant and re-run.

**High `overruns`** means the pool depth (`KOS_NS_FREELIST_MAX`) is too small
— many namespaces are alive simultaneously.  Raise the cap or investigate
whether long-lived namespaces are being held unexpectedly.


---


## 6. `CollectorNamespace` exclusion

`KosCollectorNamespace` is a subtype of `KosNamespace` used for class-body
collection.  The guard in both `ns_alloc` and `ns_dealloc`:

```c
type == &KosNamespaceType          /* ns_alloc */
Py_TYPE(self) == &KosNamespaceType /* ns_dealloc */
```

naturally excludes `CollectorNamespace`.  Class-body invocations are rare and
not on the `CALL_FAST` hot path.  Sharing the freelist between subtypes would
complicate `ns_alloc` and is not worth the marginal gain.


---


## 7. Implementation plan

All four phases are independently testable with `make test`.

### Phase 1 — freelist storage and `ns_alloc`

1. Add `KOS_NS_MIN_SLOTS` and `KOS_NS_FREELIST_MAX` constants to `namespace.h`.
2. Add `ns_freelist[]` and `ns_freecount` statics to `namespace.c`.
3. Add `#include "tracing.h"` to `namespace.c`.
4. Implement `ns_alloc` (§3.3).
5. Update `KosNamespace_Copy`: replace `PyObject_NewVar(PyObject, &KosNamespaceType, count)` with `ns_alloc(&KosNamespaceType, count)`.  Remove the explicit slot-zeroing loop (it becomes `ns_alloc`'s responsibility).
6. Update `ns_newfull_from_nspec`: replace `PyObject_NewVar(PyObject, type, count)` with `ns_alloc(type, count)`.  Remove the slot-zeroing loop.
7. `make test`.

### Phase 2 — `ns_dealloc` return path

1. Modify `ns_dealloc` (§3.4): after the existing `Py_CLEAR` loop, add the
   freelist-push guard and `KOS_TRACE_FL_RETURN` calls.
2. Update `koskari_namespace_clear` (§3.5): drain the freelist on teardown.
3. `make test`.

### Phase 3 — `tracing.c` and report

1. Add `fl_namespace` counter, `kos_trace_fl_lookup` branch, init/clear
   entry, and `ADD_FL_ENTRY` call (§5.2).
2. Build with `KOS_TRACE_FREELIST=1` and run the churn benchmark.
3. Verify the `"namespace"` section appears in the JSON report with hit counts
   matching `CALL_FAST` execution count.

### Phase 4 — tuning `KOS_NS_MIN_SLOTS`

After Phase 3, run the full benchmark suite and inspect:

- `alloc_misses / (alloc_hits + alloc_misses)` — miss rate.
- `overruns / return_hits` — depth pressure.

If the miss rate exceeds 1–2 %, double `KOS_NS_MIN_SLOTS` and rerun.  A miss
rate below 0.1 % indicates the constant is comfortably sized.  32 is the
starting point; 64 is the next candidate if needed.


---


## 8. Risks and tradeoffs

| Risk | Notes |
|---|---|
| `KOS_NS_MIN_SLOTS > actual count` wastes memory | A 32-slot slab uses 256 extra bytes over a 4-slot namespace. Acceptable given allocation churn dominates. |
| `count > KOS_NS_MIN_SLOTS` bypasses freelist | Functions with more than MIN_SLOTS locals get a fresh alloc every call. These functions are rare; overrun trace data will confirm. |
| `CollectorNamespace` from `ns_newfull_from_nspec` also calls `ns_alloc` | `ns_alloc` checks `type == &KosNamespaceType` before using the freelist. CollectorNamespace passes a different type; it gets `PyObject_NewVar` with the upsize. This is correct. |
| Freelist not thread-safe | The evaluator is single-threaded per context (GIL held). The freelist is per-module and safe under the GIL. |
| PyObject header reinit on pop | `Py_SET_REFCNT`, `Py_SET_TYPE`, `Py_SET_SIZE` are available since Python 3.9. The project already requires 3.9+. |
| Drain order in `koskari_namespace_clear` | `PyObject_Free` bypasses `tp_free`; safe because the object's Python-level refs are already cleared and `KosNamespaceType` may be in the process of being torn down. |


---


## 9. As implemented

This section records where the final implementation diverged from the proposal.
All phases of the proposal were completed, plus one additional wiring.


### 9.1 Singly-linked list via `KosNamespace_PARENT`

The proposal stored cached slabs in a static array
`ns_freelist[KOS_NS_FREELIST_MAX]`.  The implementation instead threads the
freelist through the `parent` field of each cached `KosNamespace`, forming a
singly-linked list:

```c
static KosNamespace *ns_freelist = NULL;   /* list head */
static size_t        ns_freecount = 0;
```

On push (`ns_dealloc`): `KosNamespace_PARENT(self) = (PyObject *) ns_freelist`
then `ns_freelist = (KosNamespace *) self`.

On pop (`ns_alloc`): advance the head, then `KosNamespace_PARENT(ns) = NULL`
to clear the stale link before returning.

On drain (`koskari_namespace_clear`): walk the chain, null each `parent` field,
`PyObject_Free` each slab.

This matches the `dyn_node` freelist pattern in `dynchain.c` and eliminates the
fixed-size backing array.


### 9.2 `Py_SET_TYPE` / `Py_SET_SIZE` omitted on pop

The proposal restored all three PyObject header fields on the hit path.  The
implementation omits `Py_SET_TYPE` and `Py_SET_SIZE`:

```c
Py_SET_REFCNT(p, 1);
/* ob_type and ob_size are already correct: the push guard ensures
   Py_TYPE == &KosNamespaceType and Py_SIZE == KOS_NS_MIN_SLOTS. */
```

Only `ob_refcnt` needs restoring since type and size are invariant for every
slab in the pool.


### 9.3 `ns_newfull_type` also wired

The proposal covered `KosNamespace_Copy` and `ns_newfull_from_nspec`.
`ns_newfull_type` (the Python-constructor path, `Namespace(...)`) was also
converted to `ns_alloc` and had its redundant slot-zeroing loop removed, for
full coverage.


### 9.4 Measured results

Workload: computation-heavy churn benchmarks, measured as a fraction of
equivalent CPython time.

| Benchmark | Before | After |
|---|---|---|
| churn_tree | ~300 % | ~258 % |
| churn_list_each | ~280 % | ~260 % |

Timing trace (`KOS_TRACE_TIMING=1`, `KOS_TRACE_FREELIST=0`):

| Site | Before | After | Delta |
|---|---|---|---|
| `CALL_FAST` avg | 123 ns | 120 ns | −3 ns |
| `return_boundary` avg | 187 ns | 174 ns | −13 ns |

The dealloc side (`return_boundary`) saved more than the alloc side
(`CALL_FAST`): replacing `tp_free` (pymalloc free path) with a linked-list push
is faster than replacing `PyObject_NewVar` (pymalloc alloc) with a pop.

Freelist trace (`KOS_TRACE_FREELIST=1`), 76.2 million invocations:

| Counter | Value |
|---|---|
| `alloc_hits` | 76,214,481 |
| `alloc_misses` | 19 |
| `return_hits` | 76,214,500 |
| `overruns` | 0 |

`KOS_NS_MIN_SLOTS=32` fits the workload with a 0.00 % miss rate.  No tuning
required.


---


# The end.
