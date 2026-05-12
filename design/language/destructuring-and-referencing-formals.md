# Destructuring formals, referencing formals, and `define-values` fork

## Status

Proposal — not implemented.

## Summary

### Destructuring (nested) formals

- Allow **lambda (and general procedure) formals** to contain **nested**
  structure, not only flat symbols — e.g.
  **`(lambda [a (b c) *: d] ...)`** so **`(b c)`** participates as a structured
  binding pattern in the formals list.

### Referencing formals (`(ref name)`)

- Some formals should **rebind** a local name to a caller-passed **Ref** value
  instead of only **storing** the argument value, when the formal is written in
  the designated shape.
- Example (surface uses reader sugar; internal formal uses **`(ref result)`**
  after reader macro expansion):

  - **`(define add-a [a &result] (set! result (+ a 1)))`**
  - **`(define value 100)`**
  - **`(add-a &value)`**
  - then **`value`** evaluates to **`101`**.

- The **formals reader** sees **`(ref result)`**; during **formals application**
  the inner **`result`** binding is **rebound** to the **`Ref`** passed at the
  call site; **`set!`** in that procedure scope **writes through** the **`Ref`**.

### If the two features are incompatible in one formals list

- Restrict **structured unpacking** to **`define-values`** and **`setq-values`**
  only (not general lambda formals).
- Still add **kwargs-style application** to **`define-values`** and
  **`setq-values`** in that branch.
- Do **not** add the **ref binding** feature to **`define-values`** /
  **`setq-values`** — **ref binding stays on formals only**.

## Related

- [../api/types.md](../api/types.md) — Ref variants.
- [seq-values-kwargs-formals-apply.md](seq-values-kwargs-formals-apply.md) —
  kwargs application shared with other binding sites.
