# Immortal, immutable code objects

Status: proposal. Recorded 2026-10-21. Depends on raising the supported
Python floor to 3.12, which is not scheduled.

## The gap

Function code is placed at birth into a load's arena chunks and sealed
read-only when the load completes ([bytecode.md](bytecode.md),
[execution-invariants.md](execution-invariants.md) §20). The block is
protected; the `KosCode` that names it is not. `KosCode` is a heap
`PyObject`: its refcount is written on every reference, its GC header is
threaded onto a generation list because the trailing cache slots can
close a cycle, the slots themselves are written as sites warm, and its
`code`, `pos`, `cpool`, `extra` and `extents` pointers sit in writable
memory an arbitrary write can repoint. A sealed leaf reached through a
writable header raises an attacker's cost from "flip eight bytes" to
"repoint one word at a forged block", which is a smaller step than the
seal suggests.

## The proposal

Split code objects into two classes at birth, where the compiler already
routes function code to the allocator and expression code to the heap:

- **Durable code** -- function code placed in a chunk -- becomes one
  unit: a `KosCode` whose unified block is its own tail, reserved from
  the chunk as a single slice, and sealed with the chunk. There is no
  separate block and no pointer to protect; the region offsets are
  computed from the object's own address, as `code_block_layout` already
  computes them from a base.
- **Mortal code** -- top-level expressions, definitions compiled after a
  load completes, code built under `pack-module` off -- stays what it is
  today: a heap object with a heap block, freed by refcount.

For the durable class to live on a read-only page, nothing may write it
after finalize:

1. **Refcount.** The object is immortal (PEP 683, 3.12). `Py_INCREF` and
   `Py_DECREF` on an immortal object read the count and skip the store.
   This is the version floor, and it is the one with no workaround: on
   3.9 through 3.11 every reference writes the header.
2. **GC header.** The object is a non-GC leaf. It is tracked today for
   the cache slots alone (`INVARIANTS-AND-CLARIFICATIONS.md`, Bytecode);
   with the slots moved out, nothing it reaches can form a cycle through
   it, and it is never threaded onto a generation list.
3. **Cache slots.** The one mutable field moves to a writable home. Two
   candidates: the owning `Function`, which is per-closure and mortal,
   or a side array the allocator maps writable beside each chunk, with
   the record's `slot` pointer aimed there instead of at the object's
   tail. The dispatch path is unchanged in shape either way; the
   difference is whether sites are shared across closures of one code
   (side array) or private to each (function), which is also a question
   of what the cache keys on.
4. **Weakref list.** A durable code object is immortal, so a weak
   reference to it never clears; the field is unnecessary, and the type
   can report immortal instances as not weak-referenceable or keep the
   list in the same side storage as the slots.

The chunk then holds objects, not blocks. `KosArena_Release` returns a
dead reservation's bytes while the chunk is open; an immortal object
never dies, so the release path applies to mortal code alone and the
newest-block reuse in an open chunk goes away for the durable class. A
dropped definition during load keeps its bytes, which is the measured
state today (zero dead bytes across the bootstrap and `dis` on
2026-10-21), and the seal still trims the chunk to the pages it fills.

## What it buys

After the change, the writable path to durable code is the `Function`'s
pointer to its `KosCode`. An attacker who repoints it must supply a
complete, self-consistent code object -- header, instruction records
with resolved handler addresses, pool, extents -- in memory they
control, then swing the pointer. That is a forgery, not a patch. A
relative write cannot do it; an arbitrary write with the ability to
place a crafted object already has the process by other routes. The
same shape is what V8 reached with its code pointer table, where the
table is protected by a sandbox instead of a page protection; without a
sandbox, the forgery requirement is the property on offer, and it is
the strongest one a single address space gives.

## What it costs

- **Version floor.** 3.12 for immortality. Below it the current layout
  is the only form, so either the floor rises or the durable class is a
  3.12-plus build feature with the sealed-block layout as the fallback.
- **Leaks by design.** Immortal code is never freed. Correct for the
  bootstrap and for modules that live for the process, which is nearly
  all code in a running program; wrong for anything with a lifetime.
  The class decision must be right at birth, and a module that unloads
  keeps its chunks forever.
- **Slot ownership.** Moving the slots changes where a cache lives and
  what it is shared across; the choice affects hit rates and is the one
  open design question with a performance consequence.
- **Introspection.** `code.arena`, `allocation_bytes` and the stats
  getsets report on a block; with the block inside the object they
  report on the object, and `page_rounding_bytes` has no meaning for a
  chunk member.

## Open questions

- Function-owned slots or chunk-side slots.
- Whether `.kio` decode into an allocator builds the durable class
  directly, or whether a precompiled module's code is always mortal
  until the floor rises.
- Whether the mortal class keeps the sealed-block layout at all once
  the durable class exists, or becomes plain heap with `protect-code`
  governing it alone.
