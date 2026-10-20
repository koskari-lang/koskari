# KOS_OP_MATCH_FIELD_KW

- **Style**: EXTRA (LABEL, CONST): label, attribute name.
- **Stack**: `[..., o]` -> `[..., o, a]`; jump to `a0` with nothing pushed on
  a miss.
- **Semantics**: Push the attribute `a1` of `o` read through `Datum`. An
  `AttributeError` is a miss; any other exception propagates.
- **Compiler form:** Emitted by the match compiler for each keyword field of a
  class pattern.
