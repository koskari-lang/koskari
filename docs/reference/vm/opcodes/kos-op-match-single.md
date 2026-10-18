# KOS_OP_MATCH_SINGLE

- **Style**: NOARG
- **Stack**: `[..., v]` -> `[..., cur]`; cannot miss.
- **Semantics**: A cursor over a one-element subject whose element is `v`
  itself, not read through `Datum`, replaces `v`.
- **Compiler form:** Emitted by the match compiler for a `match` arm's subject
  and for the single value an extractor, a class field or a mapping entry
  gives, when its pattern is more than one element test; one element test runs
  on the value itself.
