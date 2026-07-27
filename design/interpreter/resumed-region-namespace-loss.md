# Resumed region runs against the wrong namespace

## Status

Fixed. Reproducers are the `binding` group in
`tests/sweep/delimited_sweep.py`. Found while writing
[koskari.solve](../../koskari/solve.ki).

**Cause:** an invocation-bounded capture stops at the invocation
prompt, which drops every NS node between that prompt and the frame
below it -- the enclosing `let` / collector scopes. The synthesized
baseline frame then carried the *frame's own* namespace, so the resumed
region indexed FAST slots that namespace does not have.

**Fix:** the baseline clone carries the innermost namespace live at the
capture bound (`baseline_ns` in `kos_shift_capture`), not the frame's.

## Shape

A `let` around a `reset`, and a binding of that `let` read back inside
the region after a **multi-shot** second shift:

```koskari
(define r
  (let [[tag "T"]]
    (reset
      (begin
        (shift k (k 1))
        (shift j (begin (j 2) (j 3) "b"))
        tag))))                              ;; expect "b"
```

Three ingredients, all required:

1. an enclosing `let` (module globals are unaffected -- the module
   namespace is never in the captured segment),
2. two shifts in the region, the **second** multi-shot (making the
   second invoke a graft rather than the in-place resume),
3. a read of the `let` binding inside the region.

Drop any one and the shape passes, which is why every earlier sweep
family missed it: the existing multi-shot cases either read nothing
from an enclosing scope or keep the value on the operand stack across
the shift.

## Symptoms

One fault, three faces, depending on what the mismatched slot happens
to hold:

| Context | Symptom |
|---------|---------|
| module-level `let` | `AttributeError: 'dict' object has no attribute 'deref'` |
| inside a function frame | **SIGSEGV** |
| deeper nesting (the `solve-first` shape) | `NameError: Empty reference: NAME` |

## Diagnosis

Under gdb, the crash is in the FAST load:

```
#0 KosRef_Deref (self=0x0)            ref.c:1529
#1 kos_ns_fast_deref (ns=..., idx=0)  namespace.h:433
#2 op_FAST                            opcode/FAST.c:24
```

Inspecting the namespace at the fault:

```
ns->ob_type->tp_name          = "Namespace"
((KosNamespace*)ns)->fast_count = 0
((KosNamespace*)ns)->fast_names = ()      /* empty tuple */
((KosNamespace*)ns)->fast_raw_lo = 0
```

The live namespace has **no fast slots at all**, while the resumed
bytecode is loading slot 0. The resumed region is running against the
wrong namespace: not the `let`'s, but an empty ancestor. Everything
downstream follows from that single mismatch --
`KosNamespace_GET_FAST_VAL(ns, 0)` reads past the end,
`kos_ns_is_raw(ns, 0)` reads bit 0 of a zeroed bitmask and answers "not
raw", so `kos_ns_fast_deref` takes the reference branch and calls
`KosRef_Deref` on whatever it found -- NULL (crash), a stale object
(`'dict' has no attribute 'deref'`), or an empty cell (`Empty
reference`).

So the question is not "who cleared the binding" but **which node
provides the namespace after the second invoke lands**. Capture shares
namespaces rather than copying them (`kos_dynns_clone` increfs;
`kos_dynframe_clone` uses `KosNamespace_Reveal`, also an incref), so
the values are still alive somewhere; the live namespace register is
simply resolving to the wrong provider once the graft is on the chain.

Note `KosNamespace_Copy` is **not** implicated, but is worth knowing
about while working here: it deliberately skips raw local slots ("raw
local slots are already NULL from ns_alloc"), because it constructs a
fresh call frame rather than snapshotting a live one. It must never be
used to clone a namespace for capture.

## Guard against a false fix

`kos_ns_fast_deref` takes the reference branch with no NULL check, so a
namespace mismatch here crashes rather than reporting. Adding a NULL
check there is defensible on its own merits but must not be mistaken
for a fix: it converts a loud crash into a plausible-looking
`NameError` while the region still runs against the wrong namespace.

# The end.
