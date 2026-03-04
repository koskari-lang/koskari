# Koskari Implementation Review (2026-02)

## What looks strong right now

- The project has broad test coverage across reader/compiler/runtime/opcode/native layers, which is a good base for aggressive refactors without feature loss.
- The compiler special-form surface is substantial and already documented in design docs.

## Cleanup and simplification paths

### 1) Unify parser implementations and delete dead overlap (largest shrink opportunity)

**Current state**
- There are two large parser implementations: `koskari/reader/parser.py` and `koskari/reader/parser_ng.py`.
- The package exports `parser_ng` as the active reader path.
- Test coverage still exists for both parser variants (`test_parse.py` and `test_parse_ng.py`).

**Why this matters**
- Dual parser codepaths create long-term drift risk and duplicated bugfix cost.
- `parser.py` still has obvious hygiene issues (duplicate imports in one line), which is a maintenance smell for non-primary code.

**Suggested path**
1. Decide one canonical parser (`parser_ng` seems intended).
2. Keep only compatibility shims in `parser.py` (or remove it entirely if not part of public API stability requirements).
3. Merge tests into one shared parser contract suite with parameterized fixtures instead of near-duplicate files.
4. Add one migration release note describing removed internal parser alias paths.

**Expected payoff**
- Largest immediate source LOC reduction in Python.
- Lower reader bugfix effort and easier optimization work in one place.

---

### 2) Replace repetitive special-op wrappers with table-driven registration

**Current state**
- `koskari/compile/specials/object.py` defines many tiny wrappers that differ only by opcode and helper (`_binary_op`, `_unary_op`, `_reduce_op`).
- `koskari/compile/specials/__init__.py` has a very large manual import and `__all__` list to keep in sync.

**Suggested path**
1. Introduce a local registry table in `object.py` (symbol name, opcode, arity/reduction strategy).
2. Generate wrappers at import time via small factory helpers.
3. Reduce `__init__.py` exports to either:
   - a minimal stable API, or
   - generated `__all__` from module-local metadata.

**Expected payoff**
- Shrinks noisy boilerplate.
- Fewer merge conflicts in `__init__.py` and fewer missed export updates.
- Easier auditing of opcode/symbol mappings.

---

### 3) Resolve documentation drift around special forms

**Current state**
- `design/SPECIALS.md` still states specials are implemented in `koskari/compile/specials.py`, but implementation is now split under `koskari/compile/specials/`.

**Suggested path**
- Update the implementation-location notes in design docs to reflect package layout.
- Add a short "generated from source" section (or script) to prevent drift for symbol lists.

**Expected payoff**
- Faster onboarding and less confusion when contributors jump from docs to code.

---

### 4) Prioritize known runtime TODOs that directly affect hot paths

**Current state**
- `koskari/lib/formals.c` explicitly notes avoidable intermediary kwargs dict allocation in argument binding.
- `koskari/lib/dynchain.c` has TODOs for transfer replanning and hook scratch-stack allocation.
- `koskari/compile/specials/let.py` has a TODO for ceval-style named-let defaults.

**Suggested path**
1. **High impact**: formals kwargs path: avoid dict allocation unless required by `**kwargs` semantics.
2. **Correctness/robustness**: dynchain replan behavior when chain version changes during suspended transfers.
3. **Language ergonomics**: named-let keyword-default behavior once semantics are finalized.

**Expected payoff**
- Runtime allocation and dispatch improvements on common call paths.
- Reduced risk in continuation/dynamic-context edge cases.

---

### 5) Smooth contributor build experience (very high leverage)

**Current state**
- `make build-ext` currently depends on tooling that may be absent (`tox`, `setuptools` in environment), and handler generation is coupled to tox.

**Suggested path**
1. Add a plain-Python fallback script for generating `handlers.incl` so `tox` is optional for local extension builds.
2. Add a one-command bootstrap target (`make dev-bootstrap`) that installs minimal build/test requirements.
3. Keep tox as CI authority but decouple local edit-compile-test loop from full tox availability.

**Expected payoff**
- Faster first contribution and fewer environment-specific setup failures.

## Suggested execution order

1. Parser unification (largest reduction).
2. Table-driven special-op registration.
3. Hot-path C TODOs (formals first).
4. Build-tooling decoupling.
5. Documentation drift hardening.

## "No feature loss" guardrails for these refactors

- Keep a parser conformance test matrix (numbers, quoting, layout, reader macros, format strings).
- Add opcode/special registry snapshot tests so generated wrappers are auditable.
- Add focused perf regression checks around function-call argument binding.
- Keep one compatibility release note section for any removed internal module aliases.

