# KOS_OP_POP_NS
- **Style**: NOARG.
- **Stack**: Pop the NS node; the revealed node's namespace becomes active, and any values remaining on the NS node's segment are delivered onto the revealed segment.

**Example:** Pool: N/A.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 2     | POP_NS | 0   | 0        | pop KosDynNs; restore saved namespace |

- **Invariants**: Top of chain must be KOS_DYN_NS.
