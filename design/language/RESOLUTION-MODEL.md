# Koskari Variable Resolution Model (Proposed)

This document describes a proposed redesign of Koskari's variable resolution semantics.
The goal is to eliminate the "typo creates a variable" problem while preserving
implicit closures and global reads.

**:status:** Proposed (not yet implemented)

**:author:** Christopher O'Brien <obriencj@preoccupied.net>

**:license:** GNU General Public License v3

---

## Motivation

### Current Behavior (Scheme-like)

- **Reads:** Walk the namespace chain (local → closure → … → module). Implicit global if not found.
- **Writes (setq):** Same as reads — walk up, implicit global if not found.
- **Defines:** Promoted to front; create local binding.
- **Problem:** A typo in `(setq x 1)` creates a new global variable silently.

### Python's Behavior

- Assignment is always a local declaration unless `global` or `nonlocal` is used.
- **Problem:** A typo creates a new local variable instead.

### Desired Behavior

- **Reads:** Prefer local, walk closures upward, allow implicit global as fallback.
- **Writes:** Prefer local, walk closures upward, but **no implicit global**. Compilation error if the name was not declared locally or in a closure.
- **Explicit global writes:** New `define-global` form to opt in.

---

## Summary: Bucket Rules and Propagation

### Bucket Semantics

| Bucket | Purpose | Propagate to outer? |
|--------|---------|---------------------|
| `declares` | Local binding (define, bindq, params, loop vars) | No |
| `request_global` | Explicit need from module (define-global) | No |
| `request_for_write` | Name must be writable (setq, delq, inplace ops) | Yes |
| `request` | Name needed for read (bare symbol, refq, etc.) | Yes |

### Cleanup Rules When Adding to a Bucket

| When adding to… | Remove from… | Conflict (error) |
|-----------------|--------------|------------------|
| `declare_var(x)` | request, request_for_write, request_global | — |
| `request_global(x)` | request, request_for_write | If x in declares |
| `request_for_write(x)` | — | — |
| `request(x)` | — | — |

### Resolution Sources

| Bucket | Satisfied by |
|--------|--------------|
| `request_for_write` | declares, request_global, or closure chain only (never implicit global) |
| `request` | declares, request_global, closure chain, or implicit global (module) |

---

## Architectural Changes

### 1. Separate Module from Namespace Chain

**Current:** Parent chain is linear: function ns → … → module ns → builtins.
"Global" is "walk until you find it."

**Proposed:** Differentiate by type rather than two distinct slots.

- The parent chain is typed: each level is either a regular `Namespace` (closure)
  or `ModuleNamespace` (global boundary).
- When resolving, the type of each parent determines behavior.
- Module-level code runs in the module's namespace; functions receive their
  namespace with a typed parent (function → function → …; module is not in the
  chain for resolution semantics).

**References:**

- `koskari/lib/namespace.c` — `ns_init_syms`, `KosNamespace_FindRef`, parent chain
- `koskari/lib/func.c` — `func_build_template_ns`, `KosNamespace_FromSpec(nspec, parent_ns)`

### 2. Request-for-Write Tracking

**Current:** Single `requests` list; both reads and writes use `request_var`.
`koskari/compile/specials/variables.py` lines 58-59 (setq), 86-87 (delq).

**Proposed:** Add `request_for_write` as a distinct bucket.

- `special_setq`, `special_delq`, and inplace ops call `request_for_write(sym)`
  instead of `request_var(sym)`.
- Resolution: satisfied only by declares, request_global, or closure chain.
- Propagation: inner function's `request_for_write` merges into outer so the
  outer must provide a writable binding.

**References:**

- `koskari/compile/compiler.py` — `ByteCodeFragment.request_var` (lines 210-218)
- `koskari/compile/specials/variables.py` — `special_setq`, `special_delq`
- `koskari/compile/specials/object.py` — `special_inplace` (lines 647-676)
- `koskari/compile/specials/refs.py` — `special_refq` (read path)

### 3. Request-Global Tracking

**Current:** No explicit global; `request_var` may end up satisfied by module
when walking the parent chain.

**Proposed:** Add `request_global` bucket and `define-global` special form.

- `define-global x` (and optionally `define-global x value`) explicitly pulls
  a ref from the module and makes it available for read and write.
- When adding to `request_global(x)`: remove x from `request` and
  `request_for_write`; error if x is in `declares`.
- `request_global` does not propagate to outer (inner gets the global directly).

**References:**

- `koskari/importer/basics.ki` — `compile/set!`, `compile/ref`, `compile/bind!` (for macro patterns)
- New special: `compile/define-global` (to implement)

### 4. Declare Pruning (Existing Bug Fix)

**Current:** `declare_var` does not remove from other buckets. If a name is
requested before it is declared, it stays in `requests`, causing duplicate
slots. `koskari/compile/compiler.py` lines 221-228.

**Proposed:** When `declare_var(name)` is called, remove `name` from
`requests`, `request_for_write`, and `request_global`.

**References:**

- `koskari/compile/compiler.py` — `declare_var` (lines 221-228), `merge` (lines 286-289)

### 5. NamespaceSpec `global_syms` Slot

**Current:** `NamespaceSpec(name, local_syms, closure_syms)`.
`koskari/compile/compiler.py` lines 202-206; `koskari/lib/namespace.c` `ns_init_syms`.

**Proposed:** Add `global_syms`:

```text
NamespaceSpec(name, local_syms, closure_syms, global_syms)
```

- `local_syms` — from declares
- `closure_syms` — from chain (enclosing functions)
- `global_syms` — from request_global (resolved from module, not parent chain)

**References:**

- `koskari/lib/namespace.c` — `ns_init_syms` (lines 377-435), `KosNamespace_FromSpec`
- `koskari/lib/namespace.h` — `KosNamespaceSpec` struct
- `koskari/types.py` or equivalent — `NamespaceSpec` Python type

### 6. Compilation Resolution Order

**Proposed pass order:**

1. **Local declarations** — Process all `declare_var` (including define promotion).
2. **Explicit globals** — Process `request_global` / define-global; add to
   `global_syms`; treat as handled.
3. **Write validation** — For each `request_for_write`: must be in handled names
   or resolvable in the namespace chain (not module). Error if not.
4. **Read resolution** — For each `request`: resolve from handled, chain, or
   module (implicit global for reads).

---

## Propagation and Closure Aggregation

### Inner → Outer Propagation

When compiling a nested function, merge its `request` and `request_for_write`
into the outer fragment. The outer must satisfy these before its own body is
fully resolved.

- **request** propagates: outer must provide the name (declare, closure, or
  eventually global for reads).
- **request_for_write** propagates: outer must provide a *writable* source
  (declare, request_global, or closure chain). Never satisfied by an implicit
  read-from-global.
- **request_global** does not propagate: inner gets the global ref directly.

### Avoiding Accidental Global Write

If the outer has `request(x)` (a read) that resolves to the module, that yields
a read-only binding for write purposes. It must not satisfy the inner's
`request_for_write(x)`.

Resolution for writes uses only: declares, request_global, and the closure
chain. The read path may fall back to the module; that fallback is never used
for writes.

---

## Code References: Existing vs. Proposed

### ByteCodeFragment (compiler.py)

| Current | Proposed |
|---------|----------|
| `self.requests = []` | Add `self.request_for_write = []`, `self.request_global = []` |
| `request_var(name)` — add to requests if not in declares | Split: `request_var(name)` for reads; `request_for_write(name)` for writes |
| `declare_var(name)` — append to declares | Add cleanup: remove name from requests, request_for_write, request_global |
| `merge(other)` — merge declares, requests | Merge request_for_write; do not merge request_global |
| `as_namespace_spec()` → `(declares, requests)` | → `(declares, closure_syms, global_syms)` where closure_syms from requests (minus global resolution), global_syms from request_global |

### Special Forms (variables.py)

| Form | Current | Proposed |
|------|---------|----------|
| setq | `frag.request_var(sym)` | `frag.request_for_write(sym)`; before emit, verify sym is in declares, request_global, or will be in closure chain |
| delq | `frag.request_var(sym)` | Same as setq |
| define | `frag.declare_var(name)` | Unchanged; declare_var gains pruning |
| bindq | `frag.declare_var(name)` | Unchanged |
| (new) define-global | — | `frag.request_global(name)`; optional init value; error if name in declares |

### compile_raw_symbol (compiler.py, lines 603-631)

| Current | Proposed |
|---------|----------|
| `fragment.request_var(sym)` for reads | Unchanged (reads use request) |

### Namespace Init (namespace.c)

| Current | Proposed |
|---------|----------|
| `ns_init_syms(self, parent, local_syms, closure_syms)` | Add `global_syms`; resolve from module reference instead of parent for those |
| Closure lookup via `KosNamespace_FindRef(parent, …)` | Chain stops at module boundary; global_syms resolved via separate module ref |

### Function Creation (func.c)

| Current | Proposed |
|---------|----------|
| `KosNamespace_FromSpec(nspec, parent_ns)` — parent is linear chain | Pass module ref for global_syms resolution; parent chain typed (no module in chain) |

---

## Implementation Approach

1. **Phase 1 — Bucket infrastructure**
   - Add `request_for_write` and `request_global` to `ByteCodeFragment`.
   - Implement cleanup in `declare_var` and when adding to `request_global`.
   - Update `merge` to propagate `request_for_write` and `request` only.

2. **Phase 2 — Special form updates**
   - Change setq/delq to use `request_for_write` and validate before emit.
   - Add `compile/define-global` special form.
   - Ensure define promotion runs before write validation.

3. **Phase 3 — NamespaceSpec and C runtime**
   - Add `global_syms` to `NamespaceSpec`.
   - Update `ns_init_syms` to handle global_syms with module lookup.
   - Adjust function/namespace creation to pass module and use typed parent chain.

4. **Phase 4 — Propagation and resolution**
   - Update `merge` and child scope handling so inner requests propagate correctly.
   - Implement resolution order: declares → request_global → validate
     request_for_write → resolve request (with module fallback for reads).
   - Add compile-time error for unsatisfied request_for_write.

5. **Phase 5 — Tests**
   - Typo-in-assignment produces compile error.
   - define-global allows write to global.
   - Implicit global read still works.
   - Nested function write to outer's local works; write via outer's implicit
     global read fails.

---

## Error Messages

Suggested messages:

- Write to undeclared name:  
  `"Cannot write to %r: not declared in this scope or any enclosing scope. Use define for a local or define-global for a global."`

- request_global vs declare conflict:  
  `"%r cannot be both local (define) and global (define-global)."`

---

## Backward Compatibility

Koskari has no releases; backward compatibility is not a concern. A future
pragma (e.g. scheme-mode, python-mode) could re-enable alternate scoping
semantics if desired.

---

## Related Documents

- [design/api/NAMESPACE-AND-REFS.md](../api/NAMESPACE-AND-REFS.md) — Current namespace and ref model
- [design/interpreter/compiler.md](../interpreter/compiler.md) — Compiler architecture
- [design/SPECIALS.md](../SPECIALS.md) — Special forms reference
