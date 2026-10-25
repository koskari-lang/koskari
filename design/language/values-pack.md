# Values as the one multi-value carrier

## Status

Proposal -- not implemented. Recorded 2026-10-01 from the review item on
specials that rewrite into the bare runtime name `values`.

## Problem

Multiple values cross a seam in three shapes today:

- `return` and `yield` take a count: `RETURN n` and `YIELD n` pop n
  stacked values into a tuple (`KosContext_StackPopTail`), one value
  rides as itself.
- A keyword among the values makes the special rewrite its source to
  `(KIND (values ...))`, a call of the `values` type by name
  (`compile/specials/continuations.py`, `emit_values_form`). The name
  resolves at the use site, so `(define values ...)` in scope changes
  what `(return k: v)` does.
- `break` and `continue` carry one optional value (`BREAK 0|1`).
- The receivers, `define-values`, `setq-values` and `let-values`, unpack
  whatever the value expression yields; no opcode or receiver treats a
  values instance apart from any other sequence.

A constant in place of the name does not serve: the constant pool holds
ints, strings, atoms, tuples and slices, never a builtin type or
function, so a `.kio` could not carry it.

## Proposal

One carrier for every multi-value delivery and every receiver: the
values pack, `KosValues` (`lib/values.c`), which holds a positional
tuple, an optional keyword dict and a cached hash.

- **`compile/values`** is a special: `(values a b k: v)` compiles to the
  opcode below. As a special it outranks any binding of the name, so the
  pack is reachable from every scope, and the keyword case of `return`
  and `yield` compiles straight to the opcode with no source rewrite.
- **`BUILD_VALUES n`** builds a pack from the top n stacked values;
  **`BUILD_VALUES_NAMES (kwnames, n)`** takes the keyword tuple as
  `CALL_NAMES` does. Both are plain bytecode and serialize as such.
- **Delivery forms** build a pack: `return`, `yield`, `break` and
  `continue` with several values emit `BUILD_VALUES` then their one-value
  opcode. A single value still rides as itself.
- **Receivers** unpack a pack: `define-values`, `setq-values`,
  `let-values` and the loop targets read the pack's slots, keywords
  landing by name as a formals apply does
  ([seq-values-kwargs-formals-apply.md](seq-values-kwargs-formals-apply.md)).
  A receiver fed a plain sequence keeps today's unpack.

### Cost

`(return a b)` allocates a tuple today. A pack is that tuple inside one
small object, so the change is one allocation beside the one already
paid, not an allocation where there was none. A fused receiver row that
reads the pack's slots in place keeps the receiving side at today's
cost.

### What it settles

- The `values` rewrite in `emit_values_form` goes, with its name lookup.
- One type at every multi-value seam, which is the proving ground the
  binding-forms exploration needs before `compose-values` and shape
  dispatch over a pack are judged.

### What it does not settle

Two rewrites into runtime names stay as they are:

- A quasiquoted bracket spine rebuilds through a `bracket-cons` call by
  name (`compile/helpers.py`, `compile/specials/quasiquote.py`); a
  bracket twin of the proper-list opcode would close it.
- A keyword reroute of a reducing operator or builder rewrites `(+ 1 *:
  xs)` to `(call + 1 *: xs)`, so the operator's builtin is read by name
  where the plain form is the special. That is the extern case of
  [../namespace-externs.md](../namespace-externs.md).

## Open

- Whether `RETURN n` and `YIELD n` keep their count form for callers
  that unpack positionally, or every multi-value return builds a pack.
- Whether a pack with keywords unpacked positionally by a receiver with
  no keyword slots is an error or drops the keywords.

## Related

- [seq-values-kwargs-formals-apply.md](seq-values-kwargs-formals-apply.md)
  -- aligning a value sequence with a formals list, the receiver side.
- [../namespace-externs.md](../namespace-externs.md) -- reads of a
  module attribute by qualified name, for the sites this leaves.
- `koskari/lib/values.c`, `koskari/compile/specials/continuations.py`,
  `koskari/lib/opcode/RETURN.c`.
