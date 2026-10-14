# Scratch slots: compiler-owned locals shared by non-overlapping uses

**Status:** idea, 2026-10-14. Recorded from the match temporaries work
in [matcher-bytecode.md](matcher-bytecode.md) section 12.5; not
designed, not scheduled.

## The problem

A special that walks something needs locals of its own: the match
form's cursor, mark and repeat items, declared today as gensyms in the
enclosing scope. Each form declares its own set, so a function with
two sequence matches carries two cursors in its local table though
the two walks never overlap, and at module or class scope the names
land in that namespace and outlive the form. A namespace pushed per
form was built and measured: the push is cheap, the write-through
cell per capture is not, and the churn match_case kernel ran four
times slower.

## The idea

The frame gets scratch slots: fast locals the compiler owns, with no
user-visible name, handed out through a context manager a special
enters for the extent of its emitted code:

```python
with compiler.scratch(2) as (cursor, mark):
    ...emit against cursor and mark...
```

Inside the extent the slots are the special's; on exit they return
to the pool. Two forms whose extents do not overlap take the same
slots, so the frame holds the maximum simultaneous need, not the
sum. A nested use, a match inside an arm body or a walk inside a
walk, overlaps and takes the next slots.

## What it would settle

- The local table holds one scratch run sized to the deepest nesting,
  not one set per form.
- A scratch slot has no name a user can read, write or see in
  `vars(module)`, and nothing of it reaches a `.kio` but a count.
- Clearing at the extent's exit releases what the slot held, the last
  subject among it, at the point the special knows it is done.

## Open

- Whether scratch is a region of the frame's fast slots after the
  named locals, sized from the compile, or a separate small array the
  frame carries; the first keeps every existing fast instruction.
- Module and class scope have no frame slots; a scratch run there is
  either a hidden namespace entry or the frame of the module body's
  code, which does exist while the body runs.
- Whether the `VAR` twins need a scratch form at all: a scratch slot
  is always fast, so the emitter can emit the `FAST` twin directly and
  the processor never sees a name.
- The context manager is where overlap is known; a special that
  emits across a `yield` of its own extent, or stores a scratch value
  into a closure, breaks the discipline and needs a rule.

Ref: [matcher-bytecode.md](matcher-bytecode.md) section 12.5 for the
measured namespace layout this replaces.
