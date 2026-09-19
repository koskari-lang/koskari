# compile/datum
**Symbol:** `compile/datum`

**Syntax:** Used as `(datum expr)` or `(datum expr pos)` inside macro bodies.

**Behaviour:** **`macro-rules`** expansion to **`(call datum expr [position])`**, capturing
source position for hygiene.

See [Macro matching and hygiene](../../guide/macro-matching-and-hygiene.md).

