# Koskari Design: assert

## Status

Proposed. Depends on the pragma system ([../PRAGMA.md](../PRAGMA.md)) and its
CLI/session seed ([pragma-cli.md](pragma-cli.md)).

## Goal

Provide `assert` in two forms that share one gate:

* a **syntax** `(assert EXPR MSG?)` that compiles a check, or compiles away to a
  noop when assertions are disabled;
* a **runtime function** `assert` (a bound callable) for value/higher-order use,
  which honors the same gate at call time.

The gate is a single feature, `assert/enabled`, baseline `True`.

## `assert/enabled`

A registered pragma feature owned by a new `assert/*` namespace:

| Setting | Target | Baseline | Bare `[key]` | Domain | Effect |
|---|---|---|---|---|---|
| `assert/enabled` | compiler + session | `True` | `True` | bool | gate assertion checks |

* **Launch seed.** The value is settable at Koskari launch as a session default
  (via the CLI pragma seed — see [pragma-cli.md](pragma-cli.md)), defaulting to
  `True`. This seed becomes both the module pragma baseline *and* the
  process-level value the runtime function reads.
* **Pragma override.** A module changes the level with an ordinary declaration,
  forward-only and module-scoped:

  ```
  (pragma [assert/enabled False])
  ```

## Syntax `assert` (`compile/assert`)

A `define-special`. At **expand time** it reads the module's compile-time
`assert/enabled`:

* **enabled** → emit `EXPR`; if the result is falsey, raise `AssertionError`
  (with `MSG` when supplied), like Python's `assert`.
* **disabled** → expand to `(begin)`. `EXPR` is **not compiled or evaluated** —
  the assertion, and any side effects in it, vanish from the bytecode (Python
  `-O` semantics).

Because pragma is load-time effective and forward-only, `(pragma
[assert/enabled False])` earlier in a module disables every `assert` compiled
after it; the choice is baked per compilation, not rechecked at runtime.

## Runtime function `assert`

A bootstrap callable `(assert COND MSG?)` for when `assert` is passed as a
value, mapped, or otherwise called rather than expanded. It consults the
**session-level** `assert/enabled` (the launch seed) at call time — a
free-standing callable has no module pragma context:

* `assert/enabled` **True** → raise `AssertionError` on a falsey `COND`
  (with `MSG` when supplied);
* `assert/enabled` **False** → do **not** raise; return without effect.

Unlike the syntax noop, `COND` is already an evaluated argument here, so its
value is computed regardless; only the raise is suppressed.

## Split summary

| | reads | when | disabled behavior |
|---|---|---|---|
| syntax `assert` | module compile-time pragma | expand time | compiles to `(begin)`; `EXPR` not evaluated |
| runtime `assert` | session/launch value | call time | returns without raising; `COND` already evaluated |

## Open questions

* Graded **assert level** rather than a bare bool (e.g. `off` / `warn` /
  `error`), folding into a future `compiler/optimize` level the way
  `compiler/tco-enable` et al. are candidates to (see [../PRAGMA.md](../PRAGMA.md)
  § Future). The request names only enabled/disabled; a level is the natural
  generalization.
* Message form: single `MSG` expression vs. Python's `assert cond, msg`
  two-argument shape vs. a trailing layout body.
* Whether the runtime function should instead read a module pragma when one is
  in scope (e.g. called from within a module load), for symmetry with the
  syntax — currently it deliberately reads only the session seed.
