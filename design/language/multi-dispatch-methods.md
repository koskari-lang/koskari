# Multi-dispatch methods (matchers on formals)

## Status

Proposal — not implemented.

## Summary

- **Methods** whose implementation is chosen by **dispatch** over more than a
  single receiver convention: dispatch keys are **matchers over the method
  formals**, including **type**, **value**, and **shape** dimensions (same
  matcher vocabulary as the unified matcher story).
- Overall structure is **very similar** to how **`define-special`** is
  organized today.
- Principal stated difference from that compile-time path: multi-dispatch
  accepts **runtime sequence values** (actual arguments / seq-values at call
  time) rather than **source forms** as the subject fed to the formal matchers.

## Related

- [matcher-ng-rollup.md](../archive/matcher-ng-rollup.md) — matcher IR and invocation.
- [SPECIALS.md](../SPECIALS.md) — `compile/define-special` and special dispatch.
- [match-case-form.md](../archive/match-case-form.md) — runtime branching over matchers
  (expression-level analogue).
