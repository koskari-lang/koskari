# Scratch slots: compiler-owned locals shared by non-overlapping uses

**Status:** design, 2026-10-14. Tiers 1 and 2 are the plan; tier 3 is
an optimization to measure once they are in. Recorded from the match
temporaries work in [matcher-bytecode.md](matcher-bytecode.md)
section 12.5.

## The problem

A special that walks something needs locals of its own: the match
form's cursor, mark and repeat items, declared today as gensyms in the
enclosing scope. Each form declares its own set, so a function with
two sequence matches carries two cursors in its local table though
the two walks never overlap; at module or class scope the names land
in that namespace and outlive the form; and the gensym spellings vary
per compile in the `.kio`. A namespace pushed per form was built and
measured: the push is cheap, the write-through cell per capture is
not, and the churn match_case kernel ran four times slower.

## Tier 1: a pool of tokens in the normal locals

The fragment owns a pool and a context manager hands tokens out for
the extent of a special's emitted code:

```python
with fragment.scratch(2) as (cursor, mark):
    ...emit against cursor and mark...
```

Entering at depth `d` yields the tokens `scratch-d` and `scratch-d+1`,
declared once as ordinary locals; exiting lowers the depth, and the
next form at the same depth is handed the same tokens. A nested use,
a match inside an arm body or a walk inside a walk, overlaps and takes
the next tokens. Nothing below the compiler changes: the tokens are
locals, `convert_fast` gives them slots, the `FAST` twins address
them, and a closure can capture one like any name, so an inner
namespace that needs an outer token reaches it the ordinary way.

What it settles:

- The local table holds one scratch run sized to the deepest overlap,
  not one set per form.
- The names carry no mark, so the `.kio` is the same from one compile
  to the next.
- At module or class scope a handful of fixed names stand where one
  set per form stood.

The matcher's `temporary` and `slot_local` draw from the pool and the
gensyms go. A clear at the extent's exit, `FAST_CLEAR` or `VAR_CLEAR`
on the tokens the form used, releases what a cursor held; without it
the retention lasts until the frame returns, as for any local.

## Tier 2: their own tuple on the spec

`scratch_syms` beside the locals, closures and globals of the
`NamespaceSpec`, last in the fast-names order so no existing index
moves. The tokens leave `local_syms`: the module namespace does not
mirror them by name, `LOCALS` does not show them, and a `.kio` still
carries only names. The pool, the context manager and the emitter are
unchanged above the spec.

## Tier 3: a scratch node (optimization, to measure)

Scratch attached to a namespace is in-situ: an inner namespace, a
pushed `let` or a lambda, has slots of its own and reaches an outer
one only through a closure, which tier 1 permits by keeping the tokens
named. Unnamed register slots would lose that. A dynamic chain node
would not: every namespace pushed inside the extent shares the chain,
so an instruction reaches the nearest scratch node from anywhere in
the form.

The shape, for when it is measured:

- `PUSH_SCRATCH n` and `POP_SCRATCH`; the pop clears the `n` live
  slots in one step.
- Storage without growing the node: the union is 88 bytes and the
  inline operand segment 88, eight slots, which a scratch node never
  uses as a stack; eleven pointers in all, a `PyMem` spill past that.
- `SCRATCH k`, `SCRATCH2/3/4` to load, `SCRATCH_STORE k`, and a
  `SCRATCH` field kind for the rows whose cursor, mark or items
  operand is one, the `VAR` and `FAST` twins kept for the capture
  operand alone.
- The eval state keeps a pointer to the innermost scratch node, set at
  push, restored at pop from the node's link to the one below; a load
  is one indirection and no chain walk.
- The chain is what a multi-shot capture clones, so each resumption
  carries its own cursor state.

Cost per form execution: the push and pop from the freelist, the ten
to twenty nanoseconds the matcher design priced for a match node.
Against it, the schema change across the `MATCH_*` family and the
`dis`, kio and processor work for a new field kind. Taken only if the
measurement after tiers 1 and 2 says the retention or the clone
semantics are worth it.

Ref: [matcher-bytecode.md](matcher-bytecode.md) section 12.5 for the
measured namespace layout this replaces.
