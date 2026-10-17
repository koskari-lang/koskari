# KOS_OP_MATCH_KEY_VAR / KOS_OP_MATCH_KEY_FAST

- **Style**: EXTRA (LABEL, VAR) on `MATCH_KEY_VAR`, (LABEL, FASTVAR) on
  `MATCH_KEY_FAST`: label, key.
- **Stack**: `[..., o]` -> `[..., o, v]`; jump to `a0` with nothing pushed on
  a miss.
- **Semantics**: Push the value under the key in the variable `a1`, read by
  name on the `_VAR` form and by fast index on the `_FAST` form, in the
  mapping `o` read through `Datum`. An absent key is a miss. The mapping is
  probed as `MATCH_KEY` probes it.
- **Compiler form:** Emitted by the match compiler for a symbol key of a
  mapping pattern; the `_VAR` form pre-compile, the `_FAST` twin once
  `convert_fast` has lowered the variable.
