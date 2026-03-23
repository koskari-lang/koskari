# NamespaceSpec and ByteCodeFragment: Field Reference

Auxiliary document to [RESOLUTION-MODEL.md](RESOLUTION-MODEL.md). Describes the final
form of `NamespaceSpec` and `ByteCodeFragment`, the set of necessary fields, how
they are utilized and at what times (compile vs runtime), and how they operate
in various compilation scenarios.

**:status:** Design (proposed)

**:author:** Christopher O'Brien <obriencj@preoccupied.net>

**:license:** GNU General Public License v3

---

## Summary

- **ByteCodeFragment** is a compile-time accumulator. It holds instructions and
  bucket state (declares, requests, requests_for_write, _request_global) while
  compiling a scope. It is transient; it does not persist beyond `complete()`.

- **NamespaceSpec** is the persistent description of a scope. It is emitted into
  the constant pool and attached to `Code`. It is used at runtime to build
  namespaces. It also carries compile-time metadata needed for propagation and
  validation, which is read when completing enclosing scopes.

- **No cache.** Request data flows from fragment → NamespaceSpec → Code. When
  an enclosing scope needs a nested function's requests, it reads them from the
  nested Code's NamespaceSpec.

---

## ByteCodeFragment Fields

| Field | Type | Purpose | When Written | When Read |
|-------|------|---------|--------------|-----------|
| `name` | str | Fragment identity (e.g. `<lambda>`, `<unittest>`) | init | as_namespace_spec |
| `filename` | str or None | Source location | init | complete → Code |
| `mode` | CompilerMode or None | Compilation mode | init | — |
| `compiler` | Compiler | Builder reference | init | complete, merge |
| `instructions` | list | Raw bytecode instructions | during compile | compress, convert_fast |
| `declares` | list | Names declared in this scope (define, params, bindings) | declare_var, merge | as_namespace_spec |
| `requests` | list | Names needed for read from closure | request_var, merge | as_namespace_spec, cpool scan |
| `requests_for_write` | list | Names that must be writable (setq, delq, inplace) | request_for_write, merge | as_namespace_spec, cpool scan |
| `_request_global` | list | Names explicitly from module (define-global) | request_global, merge | as_namespace_spec |

### Bucket Semantics (recap from RESOLUTION-MODEL)

- **declares**: Local binding. Does not propagate. Cleanup removes from other buckets.
- **requests**: Closure read. Propagates to outer. Satisfied by declares, request_global, closure chain, or implicit module.
- **requests_for_write**: Must be writable. Propagates to outer. Satisfied only by declares, request_global, or closure chain (never implicit module).
- **_request_global**: Explicit module ref. Does not propagate. Conflicts with declares.

### Propagation (merge)

When `fragment.merge(other)` runs, the outer fragment adopts:
- `other.declares` → declare_var
- `other.requests` → request_var
- `other.requests_for_write` → request_for_write
- `other._request_global` → request_global (if not already declared)

### Lifecycle

1. Fragment created (e.g. `compiler.fragment(name)`).
2. Compilation populates declares, requests, requests_for_write, _request_global, instructions.
3. `as_namespace_spec()` builds a NamespaceSpec from the buckets.
4. `complete()` compresses instructions, runs cpool scan, builds Code, validates. Fragment is then discarded.

---

## NamespaceSpec Fields

| Field | Type | Purpose | Compile-Time Use | Runtime Use |
|-------|------|---------|------------------|-------------|
| `name` | str or None | Scope identity | — | Optional display |
| `local_syms` | tuple | Names declared in this scope | Validation (satisfied set) | ns_init_syms: allocate fast slots |
| `closure_syms` | tuple | Names from enclosing scope(s) | Cpool merge (propagate to parent) | ns_init_syms: resolve from parent, allocate fast slots |
| `global_syms` | tuple | Names from module (define-global) | Validation (satisfied set) | ns_init_syms: resolve from module |
| `request_for_write_syms` | tuple | Subset of closure_syms that require write | Validation, cpool merge | Not used |

### Field Derivation

Built by `ByteCodeFragment.as_namespace_spec()`:

- `local_syms` = `tuple(declares)`
- `closure_syms` = unique union of `requests` and `requests_for_write`, excluding `_request_global`
- `global_syms` = `tuple(_request_global)`
- `request_for_write_syms` = `tuple(requests_for_write)` *(proposed addition)*

### Runtime-Only Fields

`KosNamespace_FromSpec` and `ns_init_syms` use only `local_syms`, `closure_syms`, and
`global_syms` to build the runtime namespace. The slot layout is: locals, then closure
refs, then globals. `request_for_write_syms` is compile-time metadata only; it is
carried on the spec but ignored at runtime.

### Where NamespaceSpec Appears

1. **Code.namespace_spec** — The baseline spec for a function or expression's scope.
2. **Constant pool** — Emitted before PUSH_NS or PUSH_COLLECT; describes a child scope.
3. **FunctionSpec** — Wraps a Code; the Code's namespace_spec describes the function body.

---

## Cpool Scan Algorithm

`_cpool_scan_merge(fragment, instr)` iterates over compressed instructions. For each
`CONST FunctionSpec` where `FunctionSpec.code` is not None:

1. Read `closure_syms` and `request_for_write_syms` from `code.namespace_spec`.
2. For each name in `closure_syms`, call `fragment.request_var(name)`.
3. For each name in `request_for_write_syms`, call `fragment.request_for_write(name)`.

No cache. The data lives on the Code's NamespaceSpec. The inner Code was built by a
prior `complete()` of an inner fragment; that fragment's `as_namespace_spec()` produced
a NamespaceSpec with `request_for_write_syms = tuple(requests_for_write)`.

---

## Validation Algorithm

`_validate_request_for_write_tree(code, ancestor_nspecs, mode, is_root)` recurses over
the Code tree. For each nested function Code:

1. Build `satisfied` = union of `local_syms` and `global_syms` from `ancestor_nspecs`.
2. If not `is_root`, read `request_for_write_syms` from `code.namespace_spec`. For each
   name, if not in `satisfied`, raise CompilerError.
3. Build `next_ancestors` = `ancestor_nspecs` + `(code.namespace_spec,)` + all
   NamespaceSpecs in `code.constants_pool`.
4. For each FunctionSpec in `code.constants_pool`, recurse with `const.code` and
   `next_ancestors`.

Root Code skips step 2 (allows implicit global writes at module level). The
PUSH_NS NamespaceSpecs in `constants_pool` are the let/collector child scopes
between the current Code and nested functions; they contribute their
`local_syms` to `satisfied` for inner functions that close over them.

---

## Utilization Timeline

### Compile Time

| Phase | ByteCodeFragment | NamespaceSpec |
|-------|------------------|---------------|
| Compiling a scope | Receives request_var, request_for_write, declare_var, request_global; accumulates instructions | Not yet created |
| Cpool scan (during complete) | Receives propagated requests from inner FunctionSpecs | Read from inner Code.namespace_spec: closure_syms, request_for_write_syms |
| as_namespace_spec | Buckets → NamespaceSpec | Created from fragment |
| convert_fast | — | Read local_syms, closure_syms, global_syms for VAR→FAST lowering |
| Validation | — | Read from Code tree: local_syms, global_syms, request_for_write_syms |

### Runtime

| Phase | NamespaceSpec |
|-------|---------------|
| Function invocation | Code.namespace_spec → KosNamespace_FromSpec → ns_init_syms(local_syms, closure_syms, global_syms) |
| PUSH_NS execution | NSPEC from cpool → KosNamespace_FromSpec(nspec, parent) |
| PUSH_COLLECT execution | Same as PUSH_NS |

---

## Case: Top-Level Expression

**Example:** `(compile/begin (compile/setq x 1) x)`

- **Scope:** Module-level. No enclosing function.
- **Fragment:** Single fragment for the begin body. `requests_for_write = ['x']`, `requests = ['x']`, `declares = []`.
- **NamespaceSpec:** Built with `local_syms = ()`, `closure_syms = ('x',)`, `global_syms = ()`, `request_for_write_syms = ('x',)`.
- **Code:** Created with that NamespaceSpec. Compiled as EXPRESSION mode.
- **Validation:** Root Code (`is_root=True`) skips request_for_write validation. Module-level allows implicit global for writes.
- **Runtime:** Expression runs in module namespace. `x` resolves implicitly.

---

## Case: Simple Closure (Single Lambda)

**Example:** `((lambda () (compile/setq x 1)))` at top level

- **Outer:** Expression scope. Root Code.
- **Inner:** Lambda body. Fragment has `requests_for_write = ['x']`, `requests = ['x']`, `declares = []`. `x` is not in scope.
- **Complete(inner):** NamespaceSpec has `closure_syms = ('x',)`, `request_for_write_syms = ('x',)`. Code created. FunctionSpec wraps it.
- **Complete(outer):** Cpool scan finds FunctionSpec(inner_code). Reads inner_code.namespace_spec.closure_syms and request_for_write_syms. Merges into outer fragment. Outer is root → no validation. Inner is nested → validation runs: `x` not in satisfied (ancestor has no local_syms or global_syms). **Compile error.**

---

## Case: Nested Closure (Inner Writes to Outer)

**Example:** `((lambda () (define x 0) (define setter (lambda [v] (set! x v))) ...))`

- **Outer lambda:** Fragment accumulates `(define x 0)` → declares += ['x']; `(define setter ...)` → declares += ['setter']. Body compiles setter's lambda.
- **Inner lambda (setter):** Fragment has `requests_for_write = ['x']`, `requests = ['x']`, `declares = ['v']`. `x` comes from outer.
- **Complete(setter):** NamespaceSpec has `local_syms = ('v',)`, `closure_syms = ('x',)`, `request_for_write_syms = ('x',)`. Code created. FunctionSpec wraps it.
- **Complete(outer):** Cpool scan finds FunctionSpec(setter_code). Merges setter's closure_syms and request_for_write_syms into outer fragment. Outer now has requests including 'x' (already satisfied by declares). Build outer NamespaceSpec with `local_syms = ('x','setter',...)`, etc. Validation: traverse Code tree. For setter Code, ancestor_nspecs = (outer_nspec,). Satisfied = outer.local_syms ∪ outer.global_syms = {'x','setter',...}. `request_for_write_syms = ('x',)`. `x` in satisfied. **OK.**

---

## Case: Let-Style Namespace Push

**Example:** `(compile/let [[x 0]] (compile/begin ((lambda () (compile/setq x 1))) x))`

- **Outer (let):** Fragment for let. Uses `child_namespace(called_by, ['x'], [0])`.
- **Child fragment:** Receives `declare_var('x')` for the binding. Body is `(compile/begin ((lambda () (set! x 1))) x)`.
- **Inner lambda:** Fragment has `requests_for_write = ['x']`, `requests = ['x']`. `x` is in the let scope.
- **Complete(inner):** NamespaceSpec has `closure_syms = ('x',)`, `request_for_write_syms = ('x',)`. Code created.
- **Child fragment:** After body compiles, has instructions including CONST FunctionSpec(inner_code). `as_namespace_spec()` → child NamespaceSpec with `local_syms = ('x',)` (from the let binding), `closure_syms = ()`, etc.
- **child_namespace exit:** Emits CONST nspec (child's NamespaceSpec), PUSH_NS, child.instructions, POP_NS. Parent (let) fragment gets these instructions.
- **Complete(let):** The let fragment's instructions include CONST child_nspec, PUSH_NS, ... The constant pool contains both the child NamespaceSpec and the FunctionSpec(inner_code). Cpool scan: finds FunctionSpec(inner_code). Merges inner's closure_syms and request_for_write_syms into the let fragment. But the let fragment is the *parent* of the child—the child's instructions are inlined. The let fragment does not have a Code of its own in the same way; it becomes part of the expression Code. The Code's constants_pool has: child_nspec (NamespaceSpec with local_syms=('x',)), FunctionSpec(inner_code). Validation: root Code. Recurse into FunctionSpec(inner_code). For inner, ancestor_nspecs = (root_nspec, child_nspec). Child_nspec has local_syms = ('x',). Satisfied includes 'x'. **OK.**

---

## Case: Nested Let with Closure

**Example:** `(compile/let [[a 1]] (compile/let [[b 2]] (lambda () (+ a b))))`

- **Outer let:** Child fragment for inner let. Inner let's child fragment compiles the lambda.
- **Lambda:** `requests = ['a','b']`, no requests_for_write. Closure over both let bindings.
- **Complete(lambda):** NamespaceSpec has `closure_syms = ('a','b')`, `request_for_write_syms = ()`.
- **Inner let child:** NamespaceSpec has `local_syms = ('b',)`. Its instructions include CONST FunctionSpec(lambda_code). Constant pool: inner let's nspec, lambda's FunctionSpec.
- **Outer let:** Instructions include CONST outer_child_nspec, PUSH_NS, inner let instructions. Constant pool: outer child nspec (local_syms=('a',)), inner child nspec (local_syms=('b',)), FunctionSpec(lambda_code).
- **Cpool scan:** Root fragment = let fragment. Scan instructions; find CONST FunctionSpec(lambda_code). Merge lambda's closure_syms into let fragment.
- **Validation:** Root Code's constants_pool includes outer_let_nspec, inner_let_nspec, FunctionSpec(lambda_code). For lambda: ancestor_nspecs = (root_nspec, outer_let_nspec, inner_let_nspec). Satisfied = outer.local_syms ∪ inner.local_syms = {'a','b'}. Lambda's request_for_write_syms is empty. **OK.**
- **Runtime:** Expression runs in module ns. PUSH_NS creates outer let ns (a), inner let ns (b). Lambda built with inner let ns as parent. Closure lookup finds a and b. Root Code's closure_syms on the expression are irrelevant—expressions run in the given parent (module), not in a namespace built from the spec.

---

## Case: Explicit Global Declaration (define-global)

**Example:** `(compile/begin (compile/define-global x) (compile/setq x 1) x)`

- **Scope:** Module-level or inside a function.
- **Fragment:** `declare_var` is not used for x. `request_global('x')` adds x to `_request_global` and removes x from `requests` and `requests_for_write` (cleanup).
- **NamespaceSpec:** `local_syms` does not include x. `closure_syms` does not include x. `global_syms = ('x',)`. `request_for_write_syms` is empty (x satisfied by request_global).
- **Validation:** `x` in `global_syms` → in satisfied set. Write to x is allowed.
- **Runtime:** `ns_init_syms` resolves `global_syms` from the module (via `find_module_in_chain`). Each global name gets a ref borrowed from the module. Writes go to the module binding.

**Example (nested):** `((lambda () (compile/define-global counter) (compile/setq counter 0) (lambda () (compile/setq counter (+ counter 1)))))`

- **Outer lambda:** `request_global('counter')` → `_request_global = ['counter']`. Inner lambda has `requests_for_write = ['counter']`, `requests = ['counter']`.
- **Complete(inner):** NamespaceSpec has `closure_syms = ('counter',)`, `request_for_write_syms = ('counter',)`.
- **Complete(outer):** Cpool scan merges inner's buckets. Outer already has `_request_global = ['counter']`. `request_for_write('counter')` does not add (counter in _request_global). Outer NamespaceSpec: `global_syms = ('counter',)`.
- **Validation:** For inner, ancestor_nspecs = (outer_nspec,). Satisfied = outer.local_syms ∪ outer.global_syms = {'counter'}. `request_for_write_syms = ('counter',)`. counter in satisfied. **OK.**
- **Runtime:** Outer's namespace gets counter from module. Inner's namespace gets counter from outer's closure (which holds the module ref). Writes mutate the module binding.

**Example (shadowing an enclosing binding):** `((lambda () (define x 0) (lambda () (define-global x) (setq x 1))) ...)`

- **Outer lambda:** `declares = ['x']`, local x.
- **Inner lambda:** `request_global('x')` — x is not in the inner's declares, so no conflict. The inner explicitly opts into the *module's* x, bypassing the closure chain.
- **Effect:** The inner's x and the outer's x are different cells. The inner's x refers to the module binding. The outer's x remains a local closure cell. Writes in the inner go to the module; the outer's x is untouched.
- **request_global does not propagate:** The outer does not receive any request for x from the inner's define-global. The inner resolves x directly from the module at namespace init.
- **Validation:** For inner, satisfied includes global_syms = {'x'}. Write allowed. **OK.**

---

## Case: PUSH_COLLECT (Collector Namespace)

**Example:** `(compile/collector [[x y]] ...)` or class body

- **Structure:** Uses `collector_namespace` context manager. No values; names are declared empty and filled during body execution.
- **Child fragment:** Receives `declare_var` for each name. Body compiles into child.
- **collector_namespace exit:** Emits PUSH_COLLECT nspec (child's NamespaceSpec), child.instructions, POP_COLLECT. The nspec is the oparg; no CONST for values.
- **NamespaceSpec:** Same fields as PUSH_NS. `local_syms` from declares; `closure_syms` from propagated requests; `global_syms` from request_global.
- **Runtime:** CollectorNamespace created from nspec. Parent resolution same as PUSH_NS. POP_COLLECT pushes the collected namespace onto the stack.

---

## Related Documents

- [RESOLUTION-MODEL.md](RESOLUTION-MODEL.md) — Original resolution design
- [design/api/NAMESPACE-AND-REFS.md](../api/NAMESPACE-AND-REFS.md) — Namespace and ref model
</think>

<｜tool▁calls▁begin｜><｜tool▁call▁begin｜>
StrReplace