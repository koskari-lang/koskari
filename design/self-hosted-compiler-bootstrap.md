# Self-Hosted Koskari Compiler and Environment — Roadmap

This document records the **mechanical plan** for moving from today’s
Python-implemented compiler stack to a **staged, self-hosted** setup: Koskari
sources (`.ki`) compiled to loadable artifacts (`.kio`), with a **bootstrap**
compiler retained for development and for regenerating artifacts when the
language or compiler changes.

**Audience:** implementers and future prompts. It assumes familiarity with
`koskari.compile`, `koskari.importer`, and `koskari.serialize`.

**Related specifications and code:**

- `.kio` format and load strategy (softened bytecode, `complete()` on load):
  [interpreter/kio-format.md](interpreter/kio-format.md).
- Serialization implementation: `koskari.serialize` (`kio.py`, `sections.py`,
  `pool.py`, `incremental.py`).
- Importer parity test (`.ki` compile → `.kio` → exec):
  `tests/serialize/test_kio_importer.py`.

---

## 1. Goals

1. **Self-hosted compiler (semantic):** The authoritative implementation of the
   Koskari compiler (specials, expanders, driver logic you choose to port)
   lives in **Koskari source** (`.ki`), not in Python.

2. **Durable artifacts:** Compilation output is **serialized** to `.kio` files
   that the runtime can load and execute without re-parsing or re-compiling
   from `.ki` on every startup.

3. **Clear bootstrap boundary:** A **bootstrap** compiler (today’s Python
   stack) remains available for **building** the system and for **cold start**
   when no trusted `.kio` exists or when the compiler itself changes.

4. **Native VM unchanged:** The **C** implementation (`koskari.lib`, opcodes,
   `KosCode`, evaluator) remains the execution engine. Self-hosting does not
   require rewriting the VM in Koskari.

5. **Pragmatic host code:** Some **Python** (or future C glue) remains for
   embedding, packaging, import hooks, CLI, and **orchestration** of the build
   (walking directories, invoking loaded Koskari entry points). That layer is
   not the semantic core of the compiler unless you choose otherwise.

---

## 2. Current State (Baseline)

- **Compiler:** Python under `koskari/compile/` (notably `compiler.py`,
  `compile/specials/*.py`, `compile/helpers.py`, `compile/pragma`).
- **Builtin surface:** `koskari.importer.bootstrap.make_bootstrap()` registers
  all `SPECIALS` and loads `basics.ki` into the full builtins dict.
- **Serialization:** `koskari.serialize.incremental.compile_ki_file_to_kio()`
  reads a `.ki` file with the **Python** `Compiler`, completes each top-level
  form, softens, and writes a `.kio` via `dump_kio()`.
- **Import:** Loader path can execute modules from `.kio` (see tests under
  `tests/serialize/`).

---

## 3. Target Architecture

### 3.1 Package layout (conceptual)

| Segment | Role | Shipping |
|--------|------|----------|
| **`koskari.bootstrap`** (new) | Trusted **Python** compiler: `Compiler`, specials, pragma, matcher integration — whatever must exist to compile **first** from `.ki`. | **Build-time** dependency or optional extra (`pip install koskari[bootstrap]`). Not required for end users who only run prebuilt `.kio`. |
| **`koskari.compiler`** (evolves) | **Koskari** implementation of the compiler: `.ki` sources in the repo; built `.kio` artifacts for release. | **Runtime:** packaged `.kio` (and optionally `.ki` sources for debugging). |
| **`koskari.runtime` / core** | VM, `Module`, importer hooks, **minimal** loader for `.kio`, serialize **read** path. | **Always** shipped. |
| **`koskari.serialize`** | Write/read `.kio`, validation, incremental compile driver (may call bootstrap `Compiler` until self-host is complete). | Shipped if the build or runtime needs it; split read vs write if you want a smaller consumer wheel later. |

Naming is illustrative; the important part is **separation of concerns** and
**dependency direction**: bootstrap → may compile compiler sources; runtime +
`.kio` → must not depend on Python compiler sources for normal use.

### 3.2 Host boundary

Self-hosting does **not** mean “no Python.” It means:

- **Semantic compiler** runs as **Koskari bytecode** loaded from `.kio` (or is
  interpreted during development via `.ki` + bootstrap).
- **Host** provides: filesystem, `importlib` integration, `demand_call` /
  coroutine entry, and any **FFI** to Python or C that the Koskari compiler
  intentionally calls (e.g. file I/O, optional `import` of Python modules).

Document every **privileged** operation the Koskari compiler needs so the thin
host stays small and testable.

---

## 4. Build Pipeline: Two-Pass Mental Model

The mechanical story that was discussed:

### Pass A — Bootstrap (Python → `.kio`), “stage0”

**Input:** Koskari sources for the compiler tree, e.g. `koskari/compiler/**/*.ki`
(and any dependencies).

**Tooling:** A driver equivalent to today’s `compile_ki_file_to_kio`, but wired
to import `Compiler` from **`koskari.bootstrap.compile`** (not from a monolithic
`koskari.compile` path once the split exists).

**Output:** A **build directory** only — e.g. `build/stage0/koskari/compiler/…`
mirroring module layout, **one `.kio` per `.ki` file** (or one bundle per
module, depending on importer conventions). This tree is **not** the final
wheel layout; it is the **first** compiled snapshot.

**Purpose:** Prove the Koskari sources are compilable by the trusted bootstrap
and produce loadable artifacts.

### Pass B — Self-hosted (loaded `.kio` → `.kio`), “final”

**Input:** The **same** `.ki` sources as Pass A (byte-identical or
content-addressed).

**Runtime:** Do **not** use the Python `Compiler` class for the **semantic**
compile loop. Instead:

1. Load Pass A’s `.kio` modules (adjust `sys.path` / custom loader so
   `koskari.compiler.*` resolves to **stage0** implementations).
2. Invoke **Koskari-defined** entry points (e.g. `compile-file`,
   `compile-top-level-form`, or a single `rebuild-compiler-tree` driver)
   implemented in the stage0 artifacts.

**Output:** `build/final/…` or a staging tree that **becomes** packaged
`package_data` (the `.kio` files users receive).

**Purpose:** The compiler **compiles itself**. The artifact you ship is
produced by the self-hosted compiler, not only by bootstrap.

### Fixpoint / verification

- **Strong:** Byte-compare Pass A vs Pass B outputs after **canonicalization**
  (stable pool order, stable `source_filename` policy, same `dump_kio` version).
- **Practical:** Pass B twice; second run produces **identical** hashes to Pass
  B first run.
- **CI gate:** Fail release builds if fixpoint fails (or if policy allows,
  ship Pass B but **alert** on mismatch with Pass A for investigation).

Until fixpoint holds, policy may ship **Pass B** artifacts while keeping
bootstrap available for diagnosis.

---

## 5. Import Resolution During Pass B

“Compile in place” must not accidentally mix **Python** and **`.kio`**
implementations of the same logical module.

**Recommended:**

- Explicit **loader** or **`sys.path`** ordering: first `build/stage0`, then
  sources, **without** importing `koskari.bootstrap` for the compile job.
- Tests that assert **no** import of `koskari.bootstrap.compile` during Pass B
  (optional guard).

---

## 6. Ordered Implementation Steps

Work can overlap, but **dependency order** matters.

### Phase 0 — Preconditions (mostly done)

1. **Stable `.kio` write/read** aligned with [kio-format.md](interpreter/kio-format.md).
2. **Importer** can load and execute `.kio` with parity vs `.ki` for
   representative modules (`tests/serialize/test_kio_importer.py` pattern).

### Phase 1 — Repository split (no behavior change)

1. Introduce package **`koskari.bootstrap`** (or equivalent name).
2. **Move** (or re-export) from current locations:
   - `koskari/compile/*` → `koskari/bootstrap/compile/*` (or nested as you prefer).
3. Keep **temporary** compatibility shims at old import paths if needed
   (`koskari.compile` re-exports) to avoid a single giant breakage.
4. Update **`compile_ki_file_to_kio`** and tests to import `Compiler` from the
   bootstrap package.
5. **Tox / CI:** all green with shims.

### Phase 2 — Compiler sources in Koskari

1. Create **`koskari/compiler/`** (or agreed tree) containing **`.ki`**
   modules. Start with **thin** wrappers that **delegate** to existing Python
   via FFI only where unavoidable, or port **one vertical slice** (e.g. one
   special or one pass).
2. For each migrated unit, add tests: **bootstrap compile** → `.kio` → **run**
   tests against behavior.
3. Expand until the **driver** (file loop, module init) is expressible in
   Koskari.

### Phase 3 — Stage0 build automation

1. Script **`tools/build_stage0.py`** (name illustrative): for each `.ki` in
   `koskari/compiler`, run bootstrap `compile_ki_file_to_kio` into
   `build/stage0/...`.
2. **Makefile / tox env:** `stage0` target produces the tree deterministically
   (clean build directory first).

### Phase 4 — Pass B harness

1. Implement a **thin Python** (or later minimal C) **harness** that:
   - loads stage0 `.kio` for `koskari.compiler`,
   - calls the Koskari entry point to compile the same sources again into
     `build/final/...`.
2. Add **fixpoint** comparison (hashes or canonical byte comparison).

### Phase 5 — Packaging

1. **`setup.cfg` / `pyproject`:** declare which files ship:
   - `package_data` or equivalent for **`koskari/compiler/**/*.kio`** (final),
   - exclude or optional-extra **`koskari.bootstrap`**.
2. **Entry points:** CLI / `python -m` paths that use **final** `.kio` by
   default; dev install may expose bootstrap.

### Phase 6 — Decommission monolithic Python compiler from default path

1. Default import path for **users** never loads `koskari.bootstrap`.
2. Bootstrap remains in repo for **contributors** and **bootstrap builds**.
3. Document **how to regen** `.kio` when compiler sources change.

---

## 7. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Circular dependency (compiler needs features not yet in Koskari) | Port in **slices**; keep Python FFI boundaries documented; temporary dual paths. |
| Nondeterministic `.kio` bytes | Canonical pool ordering; pinned bytecode ABI in
  header; tests that hash normalized output. |
| Loader ambiguity (mixed `.py` / `.kio`) | Explicit path ordering; tests; naming (`stage0` under `build/` only). |
| Bootstrap bit-rot | CI job: **bootstrap-only** build of compiler sources on every PR. |

---

## 8. Documentation and Prompts

When opening implementation work:

- Point to this doc for **phase** and **ordering**.
- Point to [kio-format.md](interpreter/kio-format.md) for **on-disk** layout.
- Point to `koskari.serialize.incremental` for the **current** bootstrap compile
  driver.

Update this document when:

- Package names or directory layout change.
- Pass B harness API stabilizes.
- Fixpoint policy (strong vs practical) is chosen for releases.

---

## 9. Non-Goals (for this roadmap)

- Rewriting the **C VM** in Koskari.
- Eliminating **all** Python from the wheel (embedding and packaging may keep a
  thin layer).
- Changing **language semantics** unless required by self-hosting (keep those
  changes in separate design notes).

---

# The end.
