# Code Object Arena Packing: Implementation Plan

**Status:** Implemented (archived 2026-07-07; PR #90).

Companion to [code-object-arena-packing.md](code-object-arena-packing.md).
Steps are ordered by build dependency: shared C groundwork first, then the
new arena type and KosCode changes, then the pragma surface, the compiler
registry and seal, module-load integration, and finally tests and docs.
Each step leaves the tree buildable and green.

Status: implemented. Deviations from the plan as written:

* The pragma option spelling is Koskari's named-argument form
  (`max-count: 32`), confirmed end-to-end; the design doc's `:max-count`
  examples were corrected.
* The function/namespace cycle test in step 9 was reshaped: the namespace
  types do not participate in cyclic GC at all today (their `tp_traverse`
  functions exist but `HAVE_GC` is never set), so function <-> namespace
  cycles never collect -- a pre-existing defect flagged separately. The test
  now asserts refcount death of an untracked Code instead.
* Corpus records for the two pragmas are deferred: the corpus harness
  builds a bare `Compiler` without binding `__pragma__`, so pragma forms
  cannot evaluate there yet -- flagged separately as harness work.
* The loaders drop their trailing `fragment` / `expr` loop references
  before sealing, so a module's final ephemeral top-level expression is
  not swept into the arena.
* Module completion became two explicit loader flags (follow-up to the
  initial landing, replacing both the original `interactive=` flag and
  an interim `collect=` registry-handoff): `seal=True` runs
  `compiler.seal()` on successful completion, `detach=True` removes the
  load facets in the `finally`. `build_module_load_facets` reuses facets
  already bound on the module (all-or-nothing) and refreshes the
  compiler's filename per load so backtrace positions attribute to the
  current source file. The bootstrap loop loads its ten files with both
  flags off through one shared compiler, then seals once via the
  `__compiler__` facet and detaches -- one arena for all builtins, and a
  definition displaced by a later bootstrap file dies by refcount and is
  never pinned. No `gc.collect()` before that seal: a displaced function
  caught in a reference cycle would be swept in (the design's accepted
  no-cyclic-precision default), wasting only bytes in an arena the live
  builtins pin forever anyway; measured, the collection changed nothing
  (identical arena bytes with and without). The CLI's interactive path
  now passes `detach=False`, so the initial REPL module seals as the
  design's Defaults section always intended.


## Resolved preliminaries

Two questions from the design doc are settled by inspection:

* **`KosDynFrame_CODE` holds a strong reference.** The frame's code slot is
  marked owned at assignment (`koskari/lib/dynchain/dyn_frame.c:33`), cloned
  with `Py_XINCREF` (`dyn_frame.c:65-66`), released with `Py_XDECREF`
  (`dyn_frame.c:87`, `:165`), and visited by traverse (`dyn_frame.c:267`).
  The suspended-frame safety argument in the design holds. Step 5 adds a
  comment at the assignment site recording that arena packing relies on this.

* **Direct `Code(...)` construction keeps today's behavior.** The
  constructor's `protect` parameter defaults to on, so the `.kio`
  deserializer (`koskari/serialize/codex.py:362`, `:1010`) and existing tests
  that build `Code` directly are unchanged. Pragma-driven behavior flows only
  through `Compiler.complete()`.

One further hazard, beyond the design doc's quiescence requirement, gets
documented rather than solved in v1: a thread spawned during a module body
that is *actively executing* a minted function while `seal()` runs could hold
a raw instruction pointer across a GIL release inside the dispatch loop.
Suspended frames are safe (they hold `{KosCode*, pc}` and re-derive buffers on
resume via `KosCode_CODE` at `eval.c:2518`; the live pc is flushed at
`eval.c:632`); concurrently *running* frames are not. `pack_into`'s docstring
and the design doc's safety section both state the constraint: relocation
requires that no frame of the target code is mid-dispatch.


## Step 1: shared block-layout helper (C refactor, no behavior change)

The unified block's region offsets are computed twice today, with identical
arithmetic: once in `code_alloc_unified` (`koskari/lib/bytecode.c:1518`) and
once in `code_calc_alloc_stats` (`bytecode.c:366`). Relocation adds a third
consumer (recompute interior pointers from a new base), so extract the math
into one source of truth:

```c
typedef struct KosBlockLayout {
  size_t pos_offset;
  size_t cpool_offset;
  size_t extra_offset;
  size_t extent_offset;
  size_t top_offset;
  size_t total_unrounded;   /* end of top region, before page rounding */
} KosBlockLayout;

static void code_block_layout(
    uint32_t code_len, uint32_t pos_count, uint32_t cpool_len,
    uint32_t extra_len, uint32_t extent_count,
    KosBlockLayout *out);
```

Rewire `code_alloc_unified` and `code_calc_alloc_stats` over it. The existing
phase 1/3 test suites are the guard; the diff should change no observable
values.


## Step 2: `protect` flag on KosCode construction

### Flags and struct

* Add `KOS_CODE_PROTECTED = 0x04` to `KosCodeFlag` (`bytecode.h:125`),
  recording whether this object's block is currently `mprotect`-sealed and
  page-aligned. All unseal-before-free logic branches on it.

### Allocation

* `code_alloc_unified` and `code_instr_init` (`bytecode.c:2364`) gain an
  `int protect` parameter.
  * **protect on** -- today's path: page-round the total, `posix_memalign`,
    seal with `mprotect(PROT_READ)` at the end of phase 4
    (`bytecode.c:2475`), set `KOS_CODE_PROTECTED`.
  * **protect off** -- `malloc(total_unrounded)` (malloc alignment satisfies
    every region; the strictest requirement is pointer alignment for cpool),
    no page rounding, no `mprotect` on any path, flag stays clear.
    `KosCode_ALLOCATION` holds the unrounded total.

### Teardown and error paths

Every `mprotect(base, ..., PROT_WRITE)` before `free(base)` becomes
conditional on `KOS_CODE_PROTECTED`:

* the phase 3/4 error paths in `code_instr_init` (`bytecode.c:2423`, `:2433`,
  `:2446`, `:2453`, `:2461`, `:2469`, `:2476`),
* `code_dealloc` (`bytecode.c:2592`) -- after step 3, the sole teardown path
  (`code_clear` at `:2557` is deleted along with GC participation).

### Constructor surface

* `KosCode_New` (`bytecode.h:255`, `bytecode.c:2645`) gains `int protect`;
  the single caller is `code_new` (`bytecode.c:2523`).
* `code_new` switches to `PyArg_ParseTupleAndKeywords` accepting an optional
  keyword-only `protect` (default true):
  `Code(nspec, instr, name=None, filename=None, *, protect=True)`. The
  current "Code takes no named arguments" rejection is dropped.

### Observability

With protect off, `allocation_bytes` reports the unrounded total (no longer a
page multiple) -- this is the cheap Python-visible probe tests use to tell the
two modes apart. `page_rounding_bytes` already tolerates
`allocation < rounded_total` (`bytecode.c:408-414`) and reports the malloc
path sensibly.


## Step 3: KosCode weakref support; drop GC tracking

Prerequisite for the compiler registry (design: "Collection: the compiler as
registry").

### Weakref support

* Add `PyObject *weakreflist;` to `struct KosCode` (`bytecode.h:211`),
  initialized NULL in `KosCode_New`.
* Set `.tp_weaklistoffset = offsetof(KosCode, weakreflist)` on `KosCodeType`
  (`bytecode.c:2623`). Explicit slot, never `Py_TPFLAGS_MANAGED_WEAKREF`, for
  Python 3.9 portability (`koskari/lib/compat_39.h`).
* `code_dealloc` (`bytecode.c:2578`) starts with
  `if (self->weakreflist) PyObject_ClearWeakRefs(self);` before the existing
  clears.

### Drop `Py_TPFLAGS_HAVE_GC`

KosCode cannot participate in a reference cycle: everything a
compiler-minted Code reaches was created before the Code and is immutable
from Python. Cpool residents are compiler-restricted consts; the only one
that references another Code is `FunctionSpec`, whose `code` member is
READONLY (`func.c:303`) and whose inner Code is complete before the spec is
interned into the outer one, so cpool edges always point downward in
creation time. `nspec` is READONLY name tuples (`namespace.h:31`); `faux`
is a synthesized CPython code object with no path back. Cycles form through
KosFunction (code plus parent namespace holding the function), and Function
keeps its GC tracking.

* Remove `Py_TPFLAGS_HAVE_GC` from `KosCodeType` (`bytecode.c:2630`); delete
  `code_traverse` and `code_clear` (tp_clear never fires on a non-cycle
  participant), moving the block-release logic wholly into `code_dealloc`.
* `PyType_Ready` then defaults `tp_alloc`/`tp_free` to the non-GC
  allocator; no `PyObject_GC_UnTrack` is needed (it is missing today, a
  latent hazard this change dissolves).
* Heap subclasses of Code remain safe: CPython forces `HAVE_GC` and
  `subtype_traverse` (which visits `__dict__`) onto heap types.

Two escape hatches become documented leaks-on-abuse rather than collectable
cycles, both outside compiler output: a back-reference planted in the
mutable `extras` dict (the only in-tree writer stores a docstring,
`compiler.py:1061`), and a hand-built `Code(...)` whose CONST instructions
carry mutable non-const objects (the C constructor does not enforce
const-ness). Recorded in the risks log; tightening the constructor is a
separate decision, not coupled to this work.


## Step 4: `KosArenaAlloc` type (new `koskari/lib/arena.c` / `arena.h`)

New files follow C style (GPLv3 block, `/** ... */` doc comment, 2-space
indent, `// The end.`); `setup.py:88` globs `koskari/lib/*.c`, so the build
picks them up automatically.

### Struct and semantics

```c
typedef struct KosArena {
  PyObject_HEAD
  PyObject *weakreflist;   /* for lifecycle tests */
  void *base;              /* page-aligned chunk */
  size_t size;             /* total allocation, page-rounded */
  size_t fill;             /* bytes handed out */
  int sealed;
} KosArena;
```

Plain `Py_TPFLAGS_DEFAULT`: no GC participation, no traverse/clear. The arena
is a leaf owner of raw memory and holds no PyObject references, exactly as the
design requires ("non-GC", "no back-references").

### Python surface (exposed as `koskari.lib.Arena`)

* `Arena(nbytes)` -- rounds `nbytes` up to page size, `posix_memalign`,
  `memset` zero. `nbytes <= 0` raises ValueError.
* `.seal()` -- `mprotect(PROT_READ)`; ValueError if already sealed; OSError
  (from errno) on failure.
* Read-only getsets: `size` (total allocation), `used` (fill), `sealed`
  (bool). Per the design's non-goals, nothing more is exposed.
* dealloc: `PyObject_ClearWeakRefs` if needed; `mprotect(PROT_WRITE)` when
  sealed; `free(base)`.

### C API (`arena.h`)

* `KosArena_Check(obj)`
* `int KosArena_Reserve(PyObject *arena, size_t nbytes, void **out)` --
  aligns `fill` up to `sizeof(void *)`, verifies unsealed and
  `fill + nbytes <= size`, returns the slice and advances `fill`. Sets a
  Python error and returns -1 on violation.

### Registration

New unit lifecycle pair `koskari_arena_init(mod)` / `koskari_arena_clear(mod)`
wired into `koskari/lib/lib.c:74` alongside `koskari_bytecode_init`.
`koskari.lib` is the C extension module itself, so binding the type in the
module dict is the whole export story.


## Step 5: `KosCode.arena` and `pack_into`

### Struct

* Add `PyObject *arena;` to `struct KosCode`, NULL for unpacked objects.
  Accessor macro `KosCode_ARENA`. Not visited by `tp_traverse` (non-GC leaf,
  per the design).
* Read-only getset `arena` returning the owning arena or None -- the
  Python-side "is this packed" probe.

### `pack_into(arena)` method

Preconditions (each a ValueError/TypeError):

* argument is an Arena; arena is unsealed;
* `self->arena` is NULL (no double-packing);
* `self->code` is non-NULL (block exists).

Body, all under the GIL:

1. `code_block_layout(...)` from the object's stored lengths (step 1 helper).
2. `KosArena_Reserve(arena, layout.total_unrounded, &dst)`.
3. `memcpy(dst, old_base, layout.total_unrounded)` -- the unrounded extent
   only; page-rounding slack is never copied.
4. Recompute `code`, `pos`, `cpool`, `extra`, `extents`, `top_extent_id` from
   `dst` plus the layout offsets, preserving NULL for absent regions
   (`extra`, `extents`, `top_extent_id` may be NULL today and stay NULL).
5. `self->arena = Py_NewRef(arena);`
6. Release the original block: if `KOS_CODE_PROTECTED`, `mprotect` writable
   and clear the flag; `free(old_base)`.
7. `KosCode_ALLOCATION(self) = layout.total_unrounded` -- the member's
   footprint inside the arena, keeping the stats getsets meaningful.

Refcount neutrality: cpool slots are owned `PyObject *` copied verbatim by
the memcpy; ownership stays with the KosCode; freeing the old block touches
no refcounts. This matches the design's "relocation moves storage, not
references."

### Teardown branch

`code_dealloc`: when `self->arena` is non-NULL, DECREF the cpool contents as
today, `Py_CLEAR(self->arena)`, and leave the block memory alone (the arena
owns it; it is read-only and freed when the arena dies). When NULL, the
existing (now flag-conditional) unseal-and-free path runs.

### Documentation in the method

The docstring states the quiescence contract: caller holds the GIL and no
frame of this code object is mid-dispatch. Suspended frames are safe because
they hold the object plus an integer pc and re-derive the buffer on resume.


## Step 6: pragma settings (`koskari/compile/pragma/settings.py`)

Following the register-a-function pattern of the existing settings
(`settings.py:50-115`) and the baseline/bare-key distinction from
[PRAGMA.md](../PRAGMA.md):

```python
@pragma_setting("compiler/protect-code", False)
def compiler_protect_code(enabled: bool = True) -> bool:
    ...
```

Baseline off (the design's Defaults section makes off/on the module-load
default); a bare `[compiler/protect-code]` turns it on.

```python
class PackModule(NamedTuple):
    enabled: bool
    max_count: int
    max_bytes: int


@pragma_setting("compiler/pack-module", PackModule(True, 0, 0))
def compiler_pack_module(enabled=True, **options) -> PackModule:
    ...
```

Baseline on with unbounded arenas. `**options` accepts `max-count` /
`max-bytes` (and underscore spellings), validated as ints >= 0; unknown
options raise ValueError. The `**options` shape matters: the pragma macro
(`koskari/bootstrap/_pragma.ki:35-46`) packs each bracket's tail into a
`values` object whose keywords are forwarded verbatim through `PyObject_Call`
(`koskari/lib/values.c:143`), so hyphenated keyword names arrive as
non-identifier dict keys, which only a `**kwargs` parameter can receive.

Verification item during implementation: confirm the reader/call surface for
option keywords in a pragma bracket (`:max-count 32` as written in the design
versus Koskari's `max-count: 32` named-argument convention), and record the
canonical spelling in the docs and corpus examples.


## Step 7: compiler registry and `seal()` (`koskari/compile/compiler.py`)

### Registry

* `Compiler.__init__` (`compiler.py:436`): add
  `self.protect_code = False`, `self.pack_module = PackModule(True, 0, 0)`
  (mirroring the pragma baselines for direct construction), and
  `self._minted = []` -- a list of `weakref.ref`, list rather than WeakSet
  because v1 packing order is definition order.
* `reload_pragma` (`compiler.py:505`): read `compiler/protect-code` and
  `compiler/pack-module`.
* `complete()` (`compiler.py:1021`): the one choke point every KosCode passes
  through --

  ```python
  code = Code(base_nspec, instr, fragment.name, filename,
              protect=self.protect_code)
  ...
  self._minted.append(weakref.ref(code))
  ```

  This inherits the design's three properties for free: imports excluded
  structurally, nested code registered without a walk, ephemeral code
  dropping off as its weakrefs die.

### `Compiler.seal()`

```python
def seal(self):
    refs, self._minted = self._minted, []
    survivors = [c for c in (r() for r in refs)
                 if c is not None and c.arena is None]
    if not (self.pack_module.enabled and survivors):
        return ()
    ...partition...
    ...pack and seal each segment...
    return tuple(arenas)
```

* **Member size**: `code.payload_bytes + code.padding_bytes` (both existing
  getsets; their sum is the unrounded block total), aligned up to pointer
  size for the inter-member gap `KosArena_Reserve` will impose.
* **Partition** (definition order): start a new segment when adding the next
  code would exceed `max_count` (if nonzero) or `max_bytes` (if nonzero);
  a single code larger than `max_bytes` becomes a singleton segment (soft
  cap, never a placement failure). Defaults produce one segment.
* **Per segment**: `arena = Arena(total)`; `code.pack_into(arena)` for each
  member; `arena.seal()`. Sizing is exact, so after the arena allocates,
  `pack_into` cannot fail on capacity; an OSError from the final
  `mprotect` propagates with everything still in a consistent (writable
  arena) state.
* Idempotent: a second call finds an empty registry and returns `()`.
* Returns the arenas so tests and callers can introspect sizes; nothing else
  retains them (each packed code holds the strong reference that keeps its
  arena alive).


## Step 8: module-load integration (`koskari/module.py`)

The loaders already own the compiler lifecycle
(`build_module_load_facets` at `module.py:343`, detach in each `finally`),
so the seal event slots in where the design puts it: module body complete,
compiler about to be discarded.

In `load_module_async` (`module.py:138`), `load_module_demand`
(`module.py:194`), and `load_module_source_async` (`module.py:248`):

```python
            while True:
                ...read/compile/eval loop...

            if not interactive:
                compiler.seal()
        finally:
            if not interactive:
                detach_module_load_facets(module)
```

Seal runs inside the `try`, after the loop completes normally: a load that
raises skips packing (its code objects die with the failed module), while
detach still runs. Interactive sessions retain their facets and never seal,
which realizes the Defaults section (REPL lines are plain heap under the
protect-code baseline; a future CLI switch or explicit repack is out of
scope, per the design's open questions).

Consequences that need no further code:

* **Bootstrap builtins**: `make_builtins` / `make_builtins_demand`
  (`koskari/bootstrap/__init__.py`) call `load_module_demand` once per
  `_BUILTINS_SOURCES` file with a fresh compiler each time, so the builtins
  land in one sealed arena per bootstrap file under the defaults.
* **`.ki` imports** via the import hook route through these loaders.

Untouched in v1:

* `load_module_kio_async` / `load_module_kio_demand` (`module.py:298`,
  `:319`) -- no compiler exists there; codes keep constructor-default
  per-page sealing. Building the deserializer directly into an arena is the
  design's second open question and is deferred (see Follow-ups).


## Step 9: tests

Mapped to the ordered phases in `tools/pyphase.py`:

### phase_lib -- `tests/lib/test_arena.py` (new)

* Arena construction: size page-rounded up; `used` starts 0; `sealed` False;
  ValueError on nonpositive size.
* `seal()`: flips `sealed`; second call raises; deallocating a sealed arena
  does not crash (unseal-then-free path).
* `Code(protect=False)`: `allocation_bytes` is the unrounded total (not a
  page multiple) for a small code object; `protect=True` (and the default)
  stays page-multiple; both variants evaluate identically.
* `pack_into`:
  * two codes packed into one arena; `arena` getset set; `used` equals the
    sum of aligned block sizes; instructions/constants/positions identical
    before and after (compare `dis` output and cpool identity);
  * packed code executes correctly (build a function, pack, call);
  * double-pack raises; packing into a sealed arena raises;
  * lifecycle: arena weakref dies only after the last packed code dies;
    dropping one of two codes keeps the arena alive (pinning);
  * a started (suspended) generator/coroutine over a code survives packing
    and resumes correctly afterward -- the frame re-derivation property.
* KosCode weakref support: `weakref.ref(code)` works and clears on delete.
* GC status: `gc.is_tracked(code)` is False for Code instances (and stays
  False packed or unpacked); an ephemeral function/namespace cycle whose
  function holds a Code still collects, with the Code dying by refcount when
  the cycle clears.

### phase_compile / phase_specials

* Settings registration: baselines, bare-key values, option parsing
  (`max-count`, `max-bytes`, underscore spellings, rejection of unknowns).
* `Compiler.reload_pragma` picks both settings up; `complete()` passes
  `protect` through (observable via `allocation_bytes`).
* Registry: every `complete()` (root, nested lambda, class body) lands one
  weakref; ephemeral codes drop off once unreferenced.
* `seal()`: definition order preserved; partition behavior for `max_count`,
  `max_bytes`, oversize singleton; disabled pack returns `()`; idempotence;
  already-packed codes skipped.

### phase_importer / phase_reproducers

* End-to-end `load_module_source_async` across all four matrix cells
  (pragma-set variants of protect-code x pack-module):
  * off/on (default): durable functions have `arena` set post-load;
    a top-level ephemeral expression's code does not survive; module
    functions callable after seal.
  * off/off: nothing packed, nothing page-aligned.
  * on/off: today's behavior, per-object page allocations.
  * on/on: durable functions packed, originals released (allocation equals
    member footprint).
* Interactive load (`interactive=True`) leaves codes unpacked.
* Builtins smoke check: after `import koskari`, a sampling of
  koskari-written builtins (from `basics.ki` et al.) report a shared arena.

### Native (`tests/native/`)

* New `test_arena.c` in the container harness (`tests.mk`): reserve
  arithmetic, seal/unseal, pack/free ordering. Run under
  `make test-asan` / `make test-valgrind` to prove the relocate-then-free
  dance and the arena-owns-block teardown are clean.

Per the never-pin rule, any behavior found broken along the way gets a RED
reproducer asserting the correct behavior, never a characterization pin.


## Step 10: documentation and corpus

* [PRAGMA.md](../PRAGMA.md): two new rows in the settings table
  (`compiler/protect-code`, baseline `False`, bare-key `True`;
  `compiler/pack-module`, baseline on/unbounded, options `max-count` /
  `max-bytes`).
* `docs/reference/concepts/pragma.md` and
  `docs/reference/macros/compile-pragma.md`: add the settings wherever the
  existing ones are enumerated (user-facing wording; no links into
  `design/`).
* [code-object-arena-packing.md](code-object-arena-packing.md): status
  proposal -> implemented; record the resolved open question (frame code
  reference confirmed strong); leave the `.kio` and REPL-repack questions
  open with a pointer to Follow-ups here. Reword the "No cyclic-garbage
  precision" bullet: with Code untracked (step 3), the ephemeral cycle it
  describes runs function <-> namespace, with the Code hanging off it and
  dying by refcount when the cycle collects -- the sweep-into-arena
  consequence is unchanged.
* Corpus records (`docs/corpus/records/`): pragma examples for both settings
  (read/compile phase verification), including one options example, so the
  MCP server can answer for them.
* Sphinx API docs: document `Arena` alongside the other `koskari.lib` types
  if the module's types are enumerated there.


## Sequencing and validation

Suggested commit granularity, each leaving `make test` green:

1. Step 1 (layout helper refactor).
2. Steps 2-3 (protect flag + weakref/dealloc hygiene) with phase_lib tests.
3. Steps 4-5 (Arena type + pack_into) with phase_lib and native tests.
4. Step 6 (pragma settings) with phase_compile tests.
5. Steps 7-8 (registry, seal, loader integration) with importer/reproducer
   tests.
6. Step 10 (docs, corpus).

`make lint`, `make cppcheck`, and `make flawfinder` after the C steps --
flawfinder will flag the new `memcpy`/`mprotect` sites; annotate consistently
with the existing bytecode.c treatment. `make test-asan` and
`make test-valgrind` after step 5.


## Risks and decisions log

* **`KosCode_ALLOCATION` semantics widen**: page-rounded total (protected),
  unrounded total (plain heap), or member footprint (packed). Its only
  consumers are the teardown paths and the stats getsets in `bytecode.c`,
  both updated here; nothing else in the tree reads it.
* **Default flip is a real behavior change**: the pragma baselines make
  off/on the compiled-module default, so freshly loaded modules stop
  producing per-object VMAs. Tests that implicitly relied on per-object
  sealing (if any exist) surface in step 5's suite run and get updated to
  set `[compiler/protect-code]` explicitly.
* **Concurrent execution during seal**: documented constraint, not enforced.
  Single-threaded loads (the only paths that call `seal()`) satisfy it; a
  module body that leaves another thread running its own functions is
  undefined behavior for v1 and is recorded in the design doc's safety
  section.
* **Cyclic ephemeral garbage may be swept into an arena**: accepted per the
  design; no `gc.collect()` before seal.
* **Code is no longer GC-tracked**: a cycle routed *through* a Code (a
  back-reference planted in `code.extras`, or a hand-built Code holding a
  mutable non-const in cpool) becomes a permanent leak instead of
  collectable garbage. Neither is producible by the compiler; documented as
  unsupported. Also a minor introspection change: Code instances stop
  appearing in `gc.get_objects()`.
* **`mprotect` failure during `Arena.seal()`**: raises OSError; codes remain
  valid in a writable arena; no rollback attempted.
* **Pragma option spelling**: the exact keyword surface inside a pragma
  bracket needs a reader-level check during implementation (step 6
  verification item); the settings function accepts both hyphen and
  underscore forms regardless.


## Follow-ups (out of scope for v1, from the design's open questions)

* `.kio` deserializer building directly into an arena at load time -- it
  sees the whole code set and sizes up front, so it can skip
  build-then-relocate entirely. Likely the highest-value next increment for
  the compact case.
* REPL "compact accumulated definitions" repack invocation and the CLI
  switch raising REPL lines to per-line protection.
* Clustering co-living objects (a class and its methods) into the same
  arena to shrink pinning blast radius.

# The end.
