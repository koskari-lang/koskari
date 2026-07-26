# Parameter Resolution

How a parameter name becomes a value, where a value lives, and what crosses a
context fork. This supersedes D5, D6, D7 and D10 of
[archive/domain-task-isolation.md](archive/domain-task-isolation.md), which
remains the record of the task-isolation bug and the fork discipline that fixed
it.

**Status**: implemented.

---

## The rule

Resolution has three steps and no others:

1. The running context's **shadow table** (`KosContext.param_refs`), which holds
   the cells installed by enclosing `parameterize` extents.
2. The **root scope** (`KosDomain.root_refs`) of that context's domain.
3. The **default** declared by whatever named the parameter -- the `ParamRef`,
   the `parameter()` argument, or the `PARAM` instruction's operand.

With no default at step 3, resolution raises `NameError`.

Two invariants carry the design:

- **A read creates nothing.** No step materializes a cell, and no default is
  ever written into a store. Reading a name cannot change what a later read
  sees, here or in any other context.
- **A store with no enclosing extent lands in the root scope.** `parameterize`
  is the only thing that writes the shadow table.

Cross-context sharing therefore has exactly two sources: the root scope, which
is deliberate and named, and a fork taken inside an extent, which is scoped and
bounded by that extent.

## Storage

`KosDomain` owns `root_refs`, a dict of name to cell. It is never copied.
Contexts reach it by resolution through `KosContext.domain`, which every context
has: inherited from the creating context, taken from an ambient `with Domain():`
scope, or minted fresh.

`KosContext` owns `param_refs`, a dict of name to cell, lazily created. It holds
only what `parameterize` put there. At context creation it is a pointer-copy of
the creating context's table, so a child captures the extents applied at that
moment and nothing the parent enters afterward. **The `defer` is the capture
point**; `create-task` is transport and does not re-capture.

A cell is an ordinary `KosRef` (name plus value, empty when the value slot is
NULL). Cells are runtime-internal: no koskari or Python surface hands one out.

## Reading

`kos_param_lookup(ctx, key)` performs steps 1 and 2 and returns a borrowed cell
or NULL. The evaluator passes the context from its `KosEvalState`, so the hot
path never touches thread-local state. `kos_param_lookup_ambient(key)` is the
Python-side entry for callers with no eval state in hand: it discovers the
active context, falling back to the innermost python-scope domain.

Three surfaces read:

- `PARAM` (name, 0-or-1 stacked defaults) and `PARAM_CONST` (name, const
  default) -- what `parameterq` lowers to.
- `parameter(name, default=...)` -- the runtime function, for names computed at
  runtime.
- `ParamRef.deref()` -- the re-resolving accessor `define-parameter` binds.

All three implement the same three steps. They differ only in where the default
comes from.

## Writing

`parameterize` mints a fresh cell per name, swaps it into the shadow table, and
records what it displaced (or an absent-sentinel) in the PARAM node's `SAVED`
dict. Unwind swaps the prior cells back. Both operate on a context-private dict,
so LIFO discipline holds per context by construction and sibling tasks cannot
reach each other's tables.

Every other store resolves the innermost existing cell and writes it in place.
With no cell anywhere, the value is created in the root scope. So:

- `set!` inside the extent that introduced the binding writes that extent's
  cell, and the pop discards it. Reset is structural.
- `set!` with no extent in the way writes the root scope, and every context in
  the domain sees it. This is the sanctioned channel for host pre-defines and
  for values a program sets once at startup.

`kos_dynparam_clone` copies cell pointers, so multi-shot continuation
applications share cells: a store in one application is visible in the next.
This matches Racket's thread-cell behaviour and is pinned by
`tests/compile/specials/test_parameterize.py`.

## ParamRef

`ParamRef` is the only public way to address a parameter. It holds a name and a
default, and re-resolves on every use, so it is safe to hold anywhere including
module scope and closures: it always addresses the innermost binding of the
context it runs in.

**A closure over a parameter captures the name, never a cell.** Capture-at-
closure would make a closure's meaning depend on where it was constructed rather
than where it runs, which defeats the purpose of `parameterize` -- and closure
construction time is usually incidental (`map`, `sorted key=`, task bodies).
Isolation comes from the fork of the shadow table at context creation, not from
holding a cell, so nothing is lost by resolving dynamically. There is no
ref-fetch surface, which makes capture-at-resolution inexpressible rather than
merely discouraged.

The default is per-holder. Two `ParamRef`s for one name with different defaults
do not interfere, because neither default is ever stored.

## Domain

`Domain` is thread-scoped by design: it represents the contexts currently active
on a thread, and a suspending context removes itself (invariant I2 in
[archive/domain.md](archive/domain.md)). The pop-mismatch `RuntimeError` is the
intended tripwire for two tasks interleaving `Domain` blocks on one thread.

`Domain.param_scopes()` returns `{"shadow": {...}, "root": {...}}` as fresh
dicts, so tests can observe the binding topology without disturbing it. This
exists because the two tiers are otherwise unobservable, which is how an earlier
implementation stayed green while resolving through the wrong layer.

## Testing notes

A test that asserts a read equals its own declared default proves nothing: the
same answer comes back from a correct restore, a name that was never bound, and
a store layer that does nothing. Assert absence with a **no-default** `ParamRef`
and expect `NameError`.

The acceptance gate for any future change here is
`tests/reproducers/test_param_task_isolation.py` (`SLEEP_SRC`, `HANDSHAKE_SRC`,
`DROP_SRC`) and `tests/reproducers/test_param_generator_extents.py`. Those
assertions fail on a real leak.

## Future

Chain-based lookup remains the richer end state: resolution walks the dynchain
for the nearest PARAM node binding the key, and the shadow table shrinks to a
cache. The nodes already own the cells, so the migration is a lookup-path change
with the swap machinery deleted. A per-site cache validated against a table
version stamp is the natural first step, and it is invisible to the surface.

---

## References

- [archive/domain-task-isolation.md](archive/domain-task-isolation.md): the
  task-isolation bug, the reproducer, and the fork discipline
- [archive/domain.md](archive/domain.md): Domain and TLS scope machinery
- [OPCODES.md](OPCODES.md): PARAM, PARAM_CONST, PUSH_PARAM, POP_PARAM
- Racket parameterization and thread cells: the semantic model
