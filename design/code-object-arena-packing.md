# Code Object Memory: Sealing and Arena Packing

Design for how KosCode bytecode blocks are allocated, protected, and packed.
Introduces two module pragmas -- `compiler/protect-code` and
`compiler/pack-module` -- that together let a deployment trade memory footprint
against tamper-resistance without changing any source. For the pragma surface
and model see [PRAGMA.md](PRAGMA.md); for the KosCode block layout see
[interpreter/](interpreter/) and `koskari/lib/bytecode.c`.

Status: proposal.


## Motivation

Every `KosCode` today owns a single page-aligned allocation. `code_alloc_unified`
(`koskari/lib/bytecode.c:1520`) packs the instruction array, position table,
constant pool, extra-operand table, and extent tables into one block via
`posix_memalign` (`bytecode.c:1555`), then seals the whole block read-only with a
single `mprotect(PROT_READ)` (`bytecode.c:2475`). Teardown reverses it:
`mprotect(..., PROT_WRITE)` then `free` (`bytecode.c:2557`).

The seal is a hardening measure. KosCode bytecode is interpreted, never executed
as machine code, so `PROT_READ` is not a correctness requirement (there is no
W^X obligation). It exists to make an immutable code block genuinely immutable at
the OS level: a stray write or a hostile mutation of a live function's bytecode
faults instead of silently corrupting the interpreter.

The cost is severe for what it buys. Because protection is page-granular:

* A code block of a few hundred bytes still consumes a full page (typically
  4 KB). A module of many small functions rounds every one up independently.
* `mprotect` on heap memory splits the process's virtual memory area (VMA) at
  page boundaries, so every sealed code object becomes a distinct kernel VMA.
  Large programs accrue thousands of VMAs and can approach `vm.max_map_count`.
* Each object pays two `mprotect` syscalls over its lifetime.

For module loads -- the builtins, the REPL bootstrap, any imported `.ki`/`.kio`
-- this overhead is paid once per code object and never reclaimed. The two
pragmas below make the protection optional and, when wanted, cheap.


## The two pragmas

### `compiler/protect-code` (boolean)

Governs whether each individual KosCode block is sealed **at construction**.

* **on** -- `code_alloc_unified` uses the page-aligned `posix_memalign` path and
  the block is `mprotect(PROT_READ)`-sealed as it is built (today's behavior).
  The object is protected from the moment it exists.
* **off** -- the unified block is a plain `malloc`, with no page rounding, no
  `mprotect`, and no VMA split. The block is writable for its whole life.

`protect-code` takes no options. Read by the compiler at the point each KosCode
is minted, so like all pragmas it is module-scoped and forward-only: it governs
every KosCode produced after it is set. It is the only axis that touches
**ephemeral** code (top-level expressions that are evaluated and discarded),
because such code never survives to the module seal step.

### `compiler/pack-module` (boolean + options)

Governs whether, at the end of module load, the module's **durable** code
objects are relocated into shared, page-aligned, singly-sealed arenas.

* **on** -- when the module finishes loading, its surviving code objects are
  collected, copied into one or more `KosArenaAlloc` segments, and each segment
  is sealed once with a single `mprotect(PROT_READ)`. The per-object originals
  are then released.
* **off** -- no relocation. Code objects keep whatever allocation they were born
  with (governed by `protect-code`).

`pack-module` is inherently whole-module: it names a single seal event, so it
has no finer scope than the module. Its tuning options are described in
[Arena tuning options](#arena-tuning-options).

The partition to keep in mind: **ephemeral code is governed only by
`protect-code`; durable code is governed by both.**


## Behavior matrix

The four combinations, and what each produces. "Durable code" is the functions
and classes that survive into the module namespace; "ephemeral" is top-level
expressions that fall away after evaluation.

| protect-code / pack-module | Durable code ends up | Ephemeral exprs | Memory | Protected? | For |
|---|---|---|---|---|---|
| **off / off** | plain heap, unprotected | plain heap | lowest | never | general modules with no tamper-resistance need |
| **off / on** | sealed arenas (built plain, sealed once at repack) | plain heap | compact | at rest only | **default**: builtins, initial REPL module |
| **on / off** | own sealed page for life | own sealed page | worst (page/object + VMA fragmentation) | load + rest | back-compat (today's behavior) |
| **on / on** | sealed arenas (sealed per-page during load, repacked, originals freed) | own sealed page | compact at rest, highest transient peak | load + rest | full-safety |

Reading the cells:

* **off / off** -- Nothing is page-aligned or `mprotect`-ed; `code_alloc_unified`
  simply `malloc`s the unified block. Lightest and fastest to load.

* **off / on** -- During load everything is writable plain heap. At module
  completion the durable set is copied into arenas and flipped read-only once.
  Ephemeral expressions were never sealed and are not repacked. Transient peak is
  (plain copies + arenas); the copies are freed immediately after. Final state is
  compact and read-only at rest, with no per-object page tax and no during-load
  protection.

* **on / off** -- Exactly today's behavior: every code object, ephemeral or
  durable, holds its own sealed page for its whole life. Protected the entire
  time; pays a page and a VMA per object, and each ephemeral expression briefly
  burns a page.

* **on / on** -- Full-safety. Each function's bytecode is read-only from the
  moment it is defined, so a buggy or hostile module body cannot scribble an
  already-defined function before the repack. At seal, durable survivors are
  relocated into arenas, sealed once, and their per-page originals unsealed and
  freed. Highest transient peak (per-page originals and arenas coexist during the
  seal), compact and protected afterward.

Two invariants fall out of the table:

* **The two `pack-module=on` rows converge at rest.** off/on and on/on end with
  an identical resident layout (durable code in sealed arenas). They differ only
  in the load-time window -- whether durable objects were protected *during* the
  compile/eval loop -- and in transient peak. Choosing between them is purely a
  threat-model question: do you need during-load tamper-resistance?
* **The axes are orthogonal.** `protect-code` answers "seal each object as it is
  born"; `pack-module` answers "compact what the module keeps." No cell is
  degenerate, which is why two booleans is the right shape rather than one
  three-valued knob.


## Defaults

* **Module loads** default to **off / on** when the pragma is otherwise
  unspecified. This is the compact, at-rest-protected sweet spot and is the right
  default for the builtins and the initial REPL module.
* **REPL lines**, after the initial module is sealed, default to **off / off**.
  Interactive definitions are cheap plain-heap allocations. A CLI switch may
  demand more (raising REPL lines to on/off for per-line protection, or invoking
  a periodic repack of accumulated durable definitions into a fresh arena).


## Arena tuning options

`pack-module` accepts options that size the arenas it produces. Sensible defaults
serve the common case; the options exist for deployments that want to tune the
memory-versus-pinning tradeoff for their workload.

Written as a bracketed pragma fragment:

```
(pragma [compiler/pack-module])                       ; on, default sizing
(pragma [compiler/pack-module False])                 ; off
(pragma [compiler/pack-module True :max-count 32])    ; on, cap 32 objects/arena
(pragma [compiler/pack-module True :max-bytes 65536]) ; on, ~64 KB payload/arena
```

| Option | Meaning | Default |
|---|---|---|
| (positional bool) | on/off master switch | `True` |
| `max-count` | maximum number of code objects placed in one arena | `0` (unbounded: one arena per module) |
| `max-bytes` | soft cap on an arena's payload size before starting a new arena | `0` (unbounded) |

Semantics:

* **`max-count` is the primary knob.** It is a clean integer with no edge cases.
  The default of `0` (unbounded) yields a **single arena per module**, which
  minimizes VMA count and `mprotect` calls and gives the best compaction. This is
  the right default because a module's code overwhelmingly co-lives (loaded and
  dropped together).
* **`max-bytes` is a soft secondary cap.** When set, the packer starts a new
  arena once the current one's payload would exceed the cap. It is a soft target:
  a single code object larger than `max-bytes` still lands in its own singleton
  arena rather than failing to place. Both caps may be set; either one triggering
  starts a new segment.
* **When to bound.** Smaller arenas help only when partial survival is expected
  -- a few functions outliving the module (a returned closure, a function cached
  in a registry). A survivor pins its entire arena resident (see
  [Lifetime pinning](#lifetime-pinning)), so capping the arena caps the blast
  radius of any one survivor. Deployments that keep whole modules resident want
  the unbounded default; deployments that harvest a few functions from large
  transient modules may want a bound.

Packing order within the caps is definition order for v1. Clustering co-living
objects (a class and its methods) into the same arena to further reduce pinning
blast radius is a reserved future refinement and not part of v1.


## Mechanism

### `KosArenaAlloc`

A minimal, **non-GC** C type whose sole job is owning one page-aligned block:

* Constructed with a size; allocates the page-aligned chunk (`posix_memalign`).
* Callers fill it while writable, then it is sealed once (`mprotect(PROT_READ)`).
* On deallocation it unseals (`mprotect(PROT_WRITE)`) and `free`s the chunk.
* Exposes its total allocation size for introspection. Nothing more.

It holds **no** back-references to the KosCode instances that live inside it.
Ownership flows one way: each packed KosCode holds a **strong reference** to its
arena. When the last KosCode referencing an arena is cleared, the arena's
refcount reaches zero and it unseals and frees. The arena is a leaf owner of
memory only; it never participates in reference cycles and does not need to be
GC-tracked.

The block held by an arena is exactly the `code_alloc_unified` unified block,
relocated. The `KosCode` PyObject itself, and its separately allocated satellites
(`faux`, `extras`, `name`, `filename`), are untouched by packing.

### KosCode changes

Two additions to `struct KosCode` (`koskari/lib/bytecode.h:211`):

* `PyObject *arena` -- strong reference to the owning `KosArenaAlloc`, or `NULL`
  for an unpacked object. Dealloc and clear branch on it:
  * arena set -> `Py_DECREF(arena)` and do nothing to the block (the arena owns
    the memory).
  * arena `NULL` -> the existing `mprotect(PROT_WRITE); free(base)` path
    (`bytecode.c:2557`).
  `arena` is a non-GC leaf and is not visited by `tp_traverse`.
* Weakref support -- KosCode is not currently weakref-able. Add a
  `PyObject *weakreflist` slot, set `tp_weaklistoffset`, and call
  `PyObject_ClearWeakRefs` at the top of dealloc. Use the explicit slot rather
  than `Py_TPFLAGS_MANAGED_WEAKREF` for portability to Python 3.9 (see
  `koskari/lib/compat_39.h`). This is a prerequisite for the collection step
  below.

### Collection: the compiler as registry

Which code objects "belong" to a module is answered structurally, not by a
filename heuristic. There is **one `Compiler` per module**
(`koskari/compile/compiler.py:434`); per-scope `fragment`s all route back through
that single `Compiler.complete(...)` to mint their KosCode
(`compiler.py:369`, `:375`, `:389`). `complete()` is therefore the one choke
point through which every KosCode the module produces -- root form, nested
lambdas, class bodies, arbitrarily deep -- passes exactly once.

The compiler registers a **weakref** to each KosCode it mints, in a passive list
(a WeakSet or equivalent). This gives three properties for free:

* **Imports are structurally excluded.** They were minted by *their* module's
  compiler and are never in this set.
* **Nested code needs no separate walk.** Because `complete()` sees every scope,
  the set is already the complete recursive set, flat.
* **Ephemeral code drops off automatically.** A top-level expression's code
  object, once evaluated and unreferenced, is collected; its weakref goes dead.
  At seal time only the durable survivors remain live.

Weakref liveness tracks refcount reachability, which is exactly "reachable from
the module namespace," which is exactly "durable." No callbacks are wired; dead
weakrefs are filtered when the list is iterated at seal time. Dedup is inherent
because KosCode identities are unique.

### Sealing: `compiler.seal()`

"Seal the module" is "seal the live code records the module's compiler still
holds." At `compiler.seal()`:

1. Iterate the weakref list, dropping dead entries. Survivors are the durable
   set.
2. Partition survivors into segments per the `max-count` / `max-bytes` caps
   (default: one segment).
3. For each segment, allocate a writable `KosArenaAlloc` sized to the sum of the
   members' unified blocks, then for each member:
   * `memcpy` its unified block into the arena slice;
   * recompute the block's interior pointers (`code`, `pos`, `cpool`, `extra`,
     `extents`, `top_extent_id`) from the new base and the same offsets;
   * take a strong reference to the arena (`self->arena`);
   * release the original block (unsealing first if `protect-code` was on).
4. Seal the arena once (`mprotect(PROT_READ)`).
5. Discard the compiler. Its death is the seal boundary.

Relocation moves storage, not references. The `cpool` slots are copied verbatim;
they hold pointers to PyObjects living elsewhere on the heap, so the copy changes
no refcounts. Because every interior pointer is a base-relative offset already
computed in one place by `code_alloc_unified`, relocation is a `memcpy` plus a
recompute from a new base. Per-page and packed allocation are the same mechanism
differing only in arena membership: an arena of one versus an arena of N.


## Safety and invariants

* **Bytecode is immutable after construction.** The existing per-page seal only
  works because nothing rewrites a code block after it is built (no inline
  caching or bytecode quickening; if there were, today's `PROT_READ` would
  already fault). Packing inherits this property.

* **Relocation is safe against suspended frames.** A frame stores the KosCode
  *object* plus an integer `pc` (`KosDynFrame_CODE` / `KosDynFrame_PC`); the raw
  `KosOp *` is only a hot-loop cache in the eval state (`es->code`, reloaded via
  `KosCode_CODE(...)` at `bytecode`/`eval.c:2518`), and the live pc is flushed
  back onto the frame on suspend (`eval.c:632`). So a paused coroutine or a
  captured continuation holds `{KosCode*, pc}`, not a raw buffer pointer. Swapping
  a KosCode's interior pointers under the GIL causes every suspended frame to
  re-derive the new buffer on resume. Relocation must occur at a quiescent point
  (module body eval complete, no frame of the target code mid-dispatch) and under
  the GIL; at end-of-load this holds. This depends on `KosDynFrame_CODE` holding a
  **strong** reference to the KosCode -- verify before implementing.

* **Lifetime pinning.** {#lifetime-pinning}
  Once N code objects share an arena, the arena lives until the *last* of them
  dies. For module-scoped code this is the desired behavior (loaded and dropped
  together). The failure case is a single survivor -- a returned closure, a
  registered function, a suspended coroutine over a module function (which pins
  transitively via frame -> KosCode -> arena) -- keeping an otherwise-dead
  module's whole arena resident. This is the reason `max-count` / `max-bytes`
  exist. It is an accepted tradeoff, documented so nobody is surprised.

* **No cyclic-garbage precision by default.** A self-referential ephemeral
  expression (function <-> code <-> cpool cycle) stays weak-live until cyclic GC
  runs, so it could be swept into an arena at seal. This is harmless -- the arena
  holds no back-reference, so the object remains collectable afterward and the
  arena frees once its last real member dies. Forcing a `gc.collect()` before
  seal would make the live set exact, but paying a full collection on every module
  load is the worse trade; leave it off by default.


## Non-goals

* Runtime toggling of protection after module load. The pragmas are load-time and
  forward-only, per the pragma model.
* W^X or executable-page semantics. KosCode is interpreted; protection is
  integrity hardening only.
* A global arena registry or back-references from arenas to code objects.
  Introspection is per-arena size only.
* Region/`push`/`pop` scoping of either pragma; the module is the unit (use a
  submodule for finer scope), consistent with [PRAGMA.md](PRAGMA.md).


## Open questions

* Confirm `KosDynFrame_CODE` retains a strong reference to the KosCode (the
  suspended-frame safety argument depends on it).
* Whether the `.kio` deserializer should build directly into an arena at load
  time (it already sees the whole module's code set and sizes before constructing
  anything), skipping the build-then-relocate step for the compact case. This may
  be a simpler and higher-value entry point than compile-time post-processing for
  the builtins and REPL bootstrap.
* Whether REPL should expose an explicit "compact accumulated definitions" repack
  invocation, and on what trigger.

# The end.
