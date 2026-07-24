# Uniform iter / next: one access layer over both iteration protocols

**Status**: complete. All three pieces landed: the uniform opcode layer
and compiler specials, the operand-fused VAR/FAST variants, and the
koskari-defined runtime `iter`/`next` builtins (define-global in
bootstrap `_iter.ki`, bodies riding the specials; `next` distinguishes
its 1-arg raising form from an explicit default via a module-private
UNDEFINED sentinel). Deltas from the design as written: no new ITER
opcode --
OBJECT_ITER itself was upgraded to the uniform acquisition (shared with
PUSH_FOREACH via `kos_iter_acquire`), so `compile/iter` needed no
change; NEXT was renamed OBJECT_NEXT and its suspend legs corrected
(suspended python async iterator steps ride the receiving iterator
boundary -- NEXT node for OBJECT_NEXT, an AWAITING_PY FOREACH phase for
ITER_NEXT -- so mid-await values and exhaustion honor the contracts);
`compile/next` emits OBJECT_NEXT; `compile/aiter` and OBJECT_AITER were
retired (subsumed by the uniform `compile/iter`). The operand-fused
VAR_ITER / FAST_ITER and VAR_NEXT / FAST_NEXT variants are in: bare VAR
feeders fuse in `Compiler.fuse_check_feeders` (the 2-arg next form only
when the default is also a bare feeder, since the iterator load moves
past it), and `convert_fast` lowers the VAR forms to their FAST twins
through the existing auto-paired paths.

---

## Motivation

Koskari is ambiently async: protocol choice is the runtime's job, not
the author's. But the iteration entry points visible to koskari code
today are CPython's *sync* builtins, so the choice leaks:

- `(iter agen)` silently routes a koskari async generator onto the
  demand path (`tp_iter` mints a DemandGenerator) even when the caller
  is ordinary async koskari code.
- `(next agen)` raises TypeError ("not an iterator").
- Authors end up writing `(gen.__anext__)` dunders or hand-rolling an
  `anext` helper -- both observed in real scratch code.

Meanwhile `for-each` already does the right thing invisibly
(PUSH_FOREACH acquires sync-or-async and ITER_NEXT steps either), so
the uniform behavior exists -- it just has no first-class surface.

## Surface

- **`(iter x)`** -- acquire an iterator, PUSH_FOREACH's exact
  preference order (PUSH_FOREACH.c:54-66): a koskari AsyncGenerator
  passes through as itself (native, never demand-wrapped); an object
  with `am_aiter` yields its async iterator; otherwise the sync
  protocol (`PyObject_GetIter`, including the sequence fallback).
- **`(next it)`** / **`(next it default)`** -- one step of either
  protocol, ambient async: in koskari code an async step suspends and
  resumes in place; under a demand drive the drive-state rules apply
  unchanged (a genuine await denies as KoskariSyncViolation, catchable).
  Exhaustion follows the NEXT opcode's existing arity contract: 1-arg
  raises StopIteration, 2-arg yields the default. The
  boundary-exhaustion semantics (StopIteration from an in-flight
  flattened step reads as end-of-iteration) compose with no extra work.

`iter`'s two-argument callable/sentinel form is out of scope for v1.

## Design

Three pieces, created together, one core:

1. **ITER opcode** (new): the acquisition preference above, factored
   out of PUSH_FOREACH into a shared helper so the two opcodes cannot
   drift. `compile/iter` is **upgraded** from its current sync-only
   lowering (`KOS_OP_OBJECT_ITER`, specials/object.py:725) to emit
   ITER.
2. **compile/next** (new special): lowers `(next it)` and
   `(next it default)` in call position to the existing NEXT opcode
   (opcodes.incl:74) -- which already implements the combined
   kos-agen-flatten / python-iterator step with auto-await and the
   1/2-arg exhaustion contract, but which nothing currently emits.
3. **KosCallable `iter` / `next`**: bootstrap definitions whose bodies
   are the special forms -- `(define iter (lambda [x] (iter x)))`
   compiles the body through compile/iter to the ITER opcode, and the
   binding (`_bind` in bootstrap, replacing the CPython builtins in the
   builtin namespace) yields a first-class callable for HOF and value
   positions. Sharing the underlying code is therefore by construction:
   the callable *is* the opcode, one frame deep.

## Behavior matrix

| call site | iterable | result |
|---|---|---|
| koskari (async ctx) | koskari agen | native flattened iteration; awaits suspend ambient |
| koskari (async ctx) | python async iterable | aiter + async steps, ambient |
| koskari (async ctx) | python sync iterable | sync steps |
| demand koskari | any | drive-state rules: sync steps run; genuine awaits deny catchably |
| python holds the koskari callable | any | ordinary koskari-callable semantics (demand_call / coroutine) |

## Implementation sketch

- Factor PUSH_FOREACH's acquisition block into `kos_iter_acquire`
  (iter.c or eval helper); PUSH_FOREACH and the new ITER handler both
  call it.
- ITER opcode: opcodes.incl entry, handler in opcode/ITER.c,
  `tox -e handlers` regeneration, OPCODES.md, dis/assembler tables.
- compile/next special beside compile/iter (specials/object.py),
  arity 1-2 mapping to the NEXT COUNT arg.
- Bootstrap: `iter` / `next` definitions in `_iter.ki`; `_bind`
  entries replacing the CPython builtins in the builtins namespace.
  Audit in-tree `.ki` uses of the old sync `iter`/`next` (they keep
  working: sync iterables behave identically; the change is that async
  iterables stop erroring or detouring through demand).

## Testing

Matrix over the behavior table: sync iterable, python async iterable,
koskari agen, DemandGenerator; 1-arg StopIteration and 2-arg default
exhaustion; the demand rows (deny catchable, sync fallback); HOF usage
of the callables from both koskari and python; `iter` of an agen no
longer minting a DemandGenerator in koskari code.

## References

- PUSH_FOREACH.c:54-66 -- the acquisition preference to factor
- opcode/NEXT.c, opcodes.incl:74 -- the existing combined step opcode
- specials/object.py:725 -- compile/iter to upgrade
- [asyncgen-anext-await-underrun.md](asyncgen-anext-await-underrun.md)
  -- python-side anext defect, since fixed; unrelated machinery (this
  layer is koskari-side), but its fix completes the cross-boundary
  story
