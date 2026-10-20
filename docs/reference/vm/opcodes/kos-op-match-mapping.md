# KOS_OP_MATCH_MAPPING

- **Style**: LABEL
- **Stack**: `[..., o]` -> `[..., o]`; jump to the label on a miss.
- **Semantics**: The mapping gate on `o` read through `Datum`: a `dict` or a
  type flagged as a mapping passes (on Python 3.9, an instance of
  `collections.abc.Mapping`); anything else misses.
- **Compiler form:** Emitted by the match compiler for a mapping pattern.
