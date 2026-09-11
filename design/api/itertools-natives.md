# Itertools Natives

**Status:** proposal. Nothing implemented; `map` and `filter` are the
only transforms and they ship in the builtins namespace.

## What exists

`koskari/lib/transform.c` is the shared body of the `map` and `filter`
providers: one `KosCallShape` callable (or None), one acquired source
(sync or driven), a `pending` slot, a cached resume frame, a lazily
created drive context, and the `closed`/`busy` flags. It owns
construction, the throw/close/finish/pyctx suite, GC, repr, and the
async surface. A provider unit supplies only its `ki_step` and the
resume chain that step arms, staged through `kos_transform_stage`.

The step contract every provider inherits:

- A sync source element plus a plain Python callable completes in C.
- A direct-return koskari function installs through the generic call
  entry with no helper frame.
- Anything else (driven source, FRAME-shaped callable without the
  return guarantee, awaitable result) runs under one resume-carrying
  frame inside `koskari_eval`.
- Continuation capture across a live step frame refuses by name.

Binding today, in `koskari/bootstrap/__init__.py`: `_ty('map', KosMap)`
and `_ty('filter', KosFilter)` bind the native under the bare name plus a
`map?`/`filter?` predicate; Python's builtins are kept as `py-map` and
`py-filter`. `zip`, `enumerate`, `reversed`, `sorted`, and `sum` are
Python's, bound bare.

## Candidates

Two tiers, by what the native buys.

**Callable-applying.** These call a user function per element, so a
koskari function runs in the evaluator instead of crossing the Python
boundary and coming back as a coroutine. Same win `map`/`filter` have.

| Native | Extra state beyond `KosTransform` |
|---|---|
| `takewhile` | tripped flag (then exhausted, source left alone) |
| `dropwhile` | passed flag (predicate stops being consulted) |
| `starmap` | none; the element is unpacked as the argument vector |
| `accumulate` | running value, optional initial |
| `filterfalse` | none; filter with the verdict inverted |
| `groupby` | current key, current group, lookahead element |

**Source-shaping.** No callable. Python's `itertools` already does
these over sync sources, and a KosIter provider is a sync iterator
through the demand adapter, so the only win is a driven source: an
async iterator or a provider stepped natively, consumed with ambient
await and no coroutine per element.

| Native | Extra state |
|---|---|
| `zip`, `zip-longest` | N sources; fetch chain across all of them |
| `chain` | N sources, current index |
| `islice` | start/stop/step counters |
| `pairwise` | one held element |
| `batched` | count |
| `compress` | second source (selectors) |
| `product`, `tee`, `cycle`, `repeat`, `count` | buffering or no source; poor fit for the step model, out of scope |

`zip` is the one candidate that shadows a bare Python builtin binding.

## Generalizing the base

`KosTransform` stays the head of every provider struct. A provider
that needs more embeds it first and sets a larger `tp_basicsize`; its
traverse/clear call the base's then visit its own fields. Needed:

- **N sources.** `source` becomes the first of an array, or the
  multi-source providers own a `PyObject **sources` beside the base's
  single slot. The construction helper grows an arity: today
  `PyArg_UnpackTuple(args, kind, 2, 2, ...)`.
- **Optional callable** exists (`allow_none`).
- **Optional trailing arguments** (`accumulate` initial, `islice`
  bounds): the shared `tp_new` takes a per-type parser after the
  callable/source prefix.
- **Sequential fetch across driven sources.** A `zip` step over N
  driven sources chains N `KosEvalState_NextResume` requests; the resume
  state is the transform plus an index. `pending` is one slot; a
  multi-source provider needs a small element buffer.

Nothing in the step-abort or stage helpers changes.

## Placement: builtins or an importable module

Criteria: a name in the builtins namespace costs every program a
binding and, when it shadows a Python builtin, a `py-` twin; a name in
a module costs an `import-from` at the use site.

**Option A: all in builtins.** Bare `takewhile`, `islice`, ... as
koskari builtins; `zip` becomes native with `py-zip` as the twin.
Mirrors nothing in Python, where these need `import itertools`.

**Option B: an importable `koskari.itertools`.** A `.ki` module beside
`koskari/solve.ki` that does `import-from koskari.lib ...` and binds the
providers. Builtins untouched. Python readers keep their mental model
(itertools is imported), and Python's own `itertools` stays reachable
under its own name with no twin convention needed.

**Recommendation: B**, with one exception considered separately:
`zip`, because it is already bare in builtins. It moves to native only
if the driven-source case matters enough to pay for `py-zip`; the
callable-applying tier is where the evaluator win is, and `zip` is not
in it. Leave `zip` as Python's until a driven-source use case shows up.

## Initialization

`koskari.lib` is one single-phase module (`.m_slots = NULL`,
`PyModule_Create`) with a fixed init chain in `lib.c`; each unit
readies its static type and inserts it into the module dict. Adding N
providers adds N `PyType_Ready` calls plus N dict inserts to
`import koskari.lib`.

Delay options, each against that baseline:

- **Eager, in the chain.** `PyType_Ready` on a static type with no
  bases beyond `KosEvaluatedType` is microseconds. Measure
  `import koskari.lib` before and after; if the delta is under a
  millisecond, this option wins by having no mechanism.
- **Lazy readiness via module `__getattr__`.** PEP 562 applies to
  extension modules: a `__getattr__` in the module dict runs on a
  miss, so provider types could be readied and inserted on first
  access. Saves the `PyType_Ready` calls for programs that never import
  the module. New mechanism in `lib.c` for a saving bounded by the
  eager number above.
- **A second extension module.** `koskari.lib` is the only
  `Extension` in `setup.py`, and providers link against
  `kos_transform_*`, `KosEvalState_*`, and the type objects by symbol.
  A second `.so` needs either a capsule-exported C API or both modules
  linked against a shared library. Koskari has no cross-linked native
  sub-modules and this proposal does not justify starting.

Under option B the import of `koskari.itertools` itself is already
lazy: a `.ki` module is not read or compiled until imported. The type
objects in `koskari.lib` are the only eager part, and the first bullet
measures whether that is worth any mechanism at all.

**Recommendation:** eager in the chain, measured. Revisit lazy
readiness only if the measured delta is visible against the bootstrap
compile that dominates `import koskari`.

## Open questions

- Does `groupby` fit the one-frame step model, or does its lookahead
  and sub-iterator make it a second provider shape?
- `starmap` on a driven source: unpack in C from the fetched element,
  or reuse the map resume chain with a star-shaped call.
- Whether the `.ki` module also carries koskari-written combinators
  (`flatten`, `partition`) that have no native, so the module is the
  one place users look.

## Refs

- `koskari/lib/transform.h`, `transform.c`, `map.c`, `filter.c`
- `koskari/lib/lib.c` (init chain)
- `koskari/bootstrap/__init__.py` (`_ty`, `py-` twins)
- [package-layout.md](package-layout.md) (where symbols are defined)
- [../bundles-and-namespace-facets.md](../bundles-and-namespace-facets.md)
