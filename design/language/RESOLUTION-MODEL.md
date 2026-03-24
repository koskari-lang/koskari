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
- **Defines:** Create local binding; "effective" promotion via cleanup (see below).
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

The **read** path (`request`) walks the namespace chain and crosses into the
module, same as today. Only the **write** path (`request_for_write`) is bounded
to non-globals (chain only; module only via explicit `request_global`).

### Effective Promotion (Not Literal Reordering)

When `declare_var(x)` or `request_global(x)` runs, cleanup removes `x` from
other buckets. A define or define-global that appears "later" in source order
thus re-homes any prior requests for that name into the declare or global
bucket. No literal reordering of expressions is needed; the bucket cleanup
rules achieve the desired effect.

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

Closure resolution uses `KosNamespace_FindRef(parent, …)`, which walks the full
parent chain including the module. There is no separate "closure chain only" API.

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

- `define-global` mirrors `define` structure: `(define-global x)`,
  `(define-global x value)`, or `(define-global x (lambda ...))`. Same optional
  value and function forms.
- Explicitly pulls a ref from the module and makes it available for read and
  write.
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

### 6. Compilation Resolution Order and Completion Hook

When completing a fragment (e.g. in `compiler.complete`):

1. **Cpool scan** — Scan the constant pool for `FunctionSpec` values. For each,
   merge the inner `Code`'s `request` and `request_for_write` into the current
   fragment. This requires storing the inner fragment's request buckets on the
   `Code` (or equivalent) when building it from a fragment.
2. **Write validation** — For each `request_for_write`: must be in declares,
   `request_global`, or resolvable in the namespace chain (using the new
   "walk until ModuleNamespace" API). Error if not.
3. **Read resolution** — For each `request`: resolve from declares,
   `request_global`, chain, or module (implicit global for reads). Same as today.

Effective promotion is handled by bucket cleanup when `declare_var` or
`request_global` runs; no separate define pass is needed.

---

## Propagation and Closure Aggregation

### Inner → Outer Propagation (Cpool Scan)

Propagation happens at **end of compilation**, when completing a fragment. Scan
the constant pool for `FunctionSpec` values; each wraps a `Code` built from an
inner fragment. Merge the inner's `request` and `request_for_write` into the
current (outer) fragment.

To support this, the `Code` object (or equivalent) must store the inner
fragment's `request` and `request_for_write` when the Code is built from that
fragment. The completion pass then iterates over CONST instructions, extracts
`FunctionSpec` → inner `Code`, and merges the stored buckets into the outer.

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
| `merge(other)` — merge declares, requests | Merge request, request_for_write; do not merge request_global |
| `as_namespace_spec()` → `(declares, requests)` | → `(declares, closure_syms, global_syms)` where closure_syms from requests (minus global resolution), global_syms from request_global |

### Code / FunctionSpec (for cpool propagation)

When a `Code` is built from a `ByteCodeFragment`, store the fragment's
`request` and `request_for_write` on the Code (or on a FunctionSpec wrapping
it). The completion pass iterates CONST args, extracts FunctionSpec → inner
Code, and merges these stored buckets into the outer fragment before
validation.

### Special Forms (variables.py)

| Form | Current | Proposed |
|------|---------|----------|
| setq | `frag.request_var(sym)` | `frag.request_for_write(sym)`; before emit, verify sym is in declares, request_global, or will be in closure chain |
| delq | `frag.request_var(sym)` | Same as setq |
| define | `frag.declare_var(name)` | Unchanged; declare_var gains pruning |
| bindq | `frag.declare_var(name)` | Unchanged |
| (new) define-global | — | `frag.request_global(name)`; mirrors define: optional value or `(lambda ...)`; error if name in declares |

### compile_raw_symbol (compiler.py, lines 603-631)

| Current | Proposed |
|---------|----------|
| `fragment.request_var(sym)` for reads | Unchanged (reads use request) |

### Namespace Init (namespace.c)

| Current | Proposed |
|---------|----------|
| `ns_init_syms(self, parent, local_syms, closure_syms)` | Add `global_syms`; resolve from module reference instead of parent for those |
| Closure lookup via `KosNamespace_FindRef(parent, …)` | Add `global_syms`; closure uses FindRef (walks full chain including module); global_syms resolved via separate module ref |

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
   - Change setq/delq/inplace to use `request_for_write`.
   - Add `compile/define-global` special form (mirrors define: optional value or
     function form).
   - Effective promotion is handled by bucket cleanup; no separate pass needed.

3. **Phase 3 — NamespaceSpec and C runtime**
   - Add `global_syms` to `NamespaceSpec` (reshape API: 4-arg constructor).
   - Update `ns_init_syms` to handle global_syms with module lookup.
   - Add new API: resolution that walks parent chain until ModuleNamespace (for
     write validation; does not cross into module).
   - Adjust function/namespace creation to pass module and use typed parent chain.

4. **Phase 4 — Propagation and resolution**
   - Store fragment's request/request_for_write on Code when building from fragment.
   - Implement cpool scan at fragment completion: extract FunctionSpec from CONST
     args, merge inner's request/request_for_write into outer.
   - Validate request_for_write after cpool merge (using Phase 3 API); add
     compile-time error if unsatisfied.

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

- [RESOLUTION-MODEL-NAMESPEC-AND-FRAGMENT.md](RESOLUTION-MODEL-NAMESPEC-AND-FRAGMENT.md) — NamespaceSpec and ByteCodeFragment field reference, utilization timeline, case analysis
- [design/api/NAMESPACE-AND-REFS.md](../api/NAMESPACE-AND-REFS.md) — Current namespace and ref model
- [design/interpreter/compiler.md](../interpreter/compiler.md) — Compiler architecture
- [design/SPECIALS.md](../SPECIALS.md) — Special forms reference
