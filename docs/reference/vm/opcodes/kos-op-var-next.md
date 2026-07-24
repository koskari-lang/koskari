# KOS_OP_VAR_NEXT
- **Style**: EXTRA (VAR, COUNT). Row = iterator name (pool), n (1 or 2).
- **Status**: Implemented.
- **Stack**: n=1: `[...] -> [..., value]`; n=2: `[..., default] -> [..., value-or-default]`. Fused VAR + OBJECT_NEXT: resolve the iterator name and run the shared step core (`object_next_step`) -- Koskari agen flatten, ambient async (a suspended python step rides the NEXT boundary), 1-arg raises StopIteration on exhaustion, 2-arg pops and delivers the default.
- **Invariants**: The fused name resolution runs after the default row (the fusion requires a bare default feeder, so the reorder is unobservable beyond NameError timing).
- **Compiler form:** Produced by `Compiler.fuse_check_feeders` when OBJECT_NEXT's iterator feeder is a bare VAR load (the 2-arg form also requires a bare VAR/CONST default). Lowers to FAST_NEXT through the generic schema-driven `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/VAR_NEXT.c` (cold handler)
