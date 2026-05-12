# Seq-values with kwargs (in-scope formals apply)

## Status

Proposal — not implemented.

## Summary

- Extend **seq-values** (or the value sequence / application path that carries
  runtime call arguments) so it can accept **kwargs** as well as positional
  slices.
- Semantics: treat the call as an **in-scope formals apply** — align the
  incoming sequence with a known formal list **in the current scope**, including
  keyword slots.
- **Standalone** language/VM feature: useful beyond a single consumer.
- **Foundation** for **runtime `match`** / **match/case** arm binding: matched
  subvalues need a stable way to land under names in an arm-local environment;
  kwargs-shaped application matches that need.

## Related

- [match-case-form.md](match-case-form.md) — runtime match form (depends on
  this foundation).
- Formals and **`FormalsSpec`** implementation paths under `koskari` (see
  [../api/types.md](../api/types.md) and compiler specials for lambda/apply).
