# KOS_OP_MATCH_CASE

- **Style**: EXTRA. Schema (LABEL, CONST, CONST, COUNT).
- **Status**: retired. The one-instruction form of a `match` arm over
  the matcher that preceded the `MATCH_*` family
  ([section 9.2](../index.md#92-pattern-matching)). The opcode keeps its
  number in the table; the compiler never emits it, and a program that
  reaches it raises `RuntimeError`.
