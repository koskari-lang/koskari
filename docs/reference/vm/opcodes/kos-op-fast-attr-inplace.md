# KOS_OP_FAST_ATTR_INPLACE

- **Style**: EXTRA (OPCOUNT, FASTVAR, CONST, NOARG). Row a0 = operator selector and count, a1 = the object's fast slot, a2 = the attribute name.
- **Stack**: `[..., v0 .. v_{n-1}] → [...]`. Read the object from its slot, reduce obj.attr in place with the count values left to right, store it back; pushes nothing. Hot: prologue only, tail-calling the body the elector slot holds, which guards through the field slot and reads, computes, and stores through its location; an empty elector slot or an ungated site takes the electing function, and the generic path runs `kos_attr_inplace_cached` through the field slot.
- **Compiler form:** The lowered form of **VAR_ATTR_INPLACE**.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR_INPLACE.c`
