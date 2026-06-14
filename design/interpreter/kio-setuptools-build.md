# `.kio` precompilation in setuptools wheel builds

## Status

**Design proposal** — not implemented.

Companion to [kio-format.md](kio-format.md) (on-disk layout) and
[self-hosted-compiler-bootstrap.md](../self-hosted-compiler-bootstrap.md)
(long-term self-hosting). This document covers **near-term packaging**: compile
`.ki` → `.kio` during **`bdist_wheel`**, ship **sources in sdist**, **add**
**`.kio` artifacts beside `.ki` in wheels** (never remove `.ki`), and teach
loaders to **prefer `.kio`** when both are present.

---

## Goals

1. **Faster cold start** for installed wheels (skip parse/compile of bootstrap
   and other packaged `.ki` at import time).
2. **Source-first sdist** — sdists contain `.ki` (and C sources) only; no
   committed or generated `.kio` in the release tarball.
3. **Wheel-only compile** — `.kio` generation runs as the last substantive step
   before the wheel is zipped, after `build_ext` and `build_py`.
4. **Dev unchanged** — editable installs and working-tree runs continue to use
   `.ki` unless a local cache policy is enabled (optional future work).
5. **One driver** — the same compile logic used by `tools/regen_kio.py` (to be
   written) and by the setuptools hook.
6. **Additive only** — wheel builds **add** sibling `.kio` files next to `.ki`;
   they **never delete or omit** `.ki` from `build_lib` or the wheel. Sources
   remain available for `linecache`, debugging, and `KOSKARI_SOURCE=1`.

Non-goals for v1:

* Self-hosted Pass B (compiler compiling itself) — see
  [self-hosted-compiler-bootstrap.md](../self-hosted-compiler-bootstrap.md).
* Precompiling third-party **`koskari_scripts`** wheels automatically (may reuse
  the same hook later; not required for koskari's own wheel).

---

## Distribution split: sdist vs wheel

| Artifact | `.ki` | `.kio` | Notes |
| -------- | ----- | ------ | ----- |
| **Git checkout** | yes | no (gitignored) | Contributors edit `.ki`. |
| **sdist** | yes | no | Collected from source tree + `package_data` globs. |
| **wheel** | yes | yes (added at build) | `build_py` copies `.ki`; `bdist_wheel` adds sibling `.kio`. |
| **editable install** | yes | optional local cache | No `bdist_wheel`; uses source tree. |

Rationale: sdist consumers and downstream packagers need Koskari **source**.
Wheels add precompiled `.kio` for fast import while **retaining `.ki`** for
tracebacks and source fallback.

### Additive-only policy

Wheel precompilation is strictly **additive**:

* **`build_py`** copies `.ki` into `build_lib` unchanged.
* **`compile_kio_tree`** writes **`foo.kio`** beside **`foo.ki`** in the same
  directory.
* **No step removes, renames, or excludes `.ki`** from `build_lib` before the
  wheel is packed.

Runtime loaders **prefer `.kio`** when both exist; `.ki` remains the source of
truth for `get_source` / `linecache` and for `KOSKARI_SOURCE=1` overrides.

### Why not put `.kio` in sdist?

* Duplicates artifacts and sources in one tarball.
* Invites stale `.kio` if someone runs regen locally and forgets to strip.
* Wheel build from sdist already has everything needed to compile (C ext +
  Python bootstrap compiler in the same tree).

### sdist hygiene

Extend **`kos_sdist`** (or equivalent) to **delete any `*.kio`** from the
release tree after `make_release_tree`, as a guard when local regen left
artifacts in the working copy.

**`MANIFEST.in`:** do not `recursive-include` `*.kio`.

**`setup.cfg` `package_data`:** list `*.ki` (current layout). Add `*.kio` so
wheels include generated artifacts once `compile_kio_tree` writes them into
`build_lib`. Sdist collection still sees no `.kio` in the source tree.

---

## Setuptools integration

### Command to hook: `bdist_wheel`

Use a custom **`kos_bdist_wheel`** subclass of `setuptools.command.bdist_wheel.bdist_wheel`.

**Do not** hook only `build_py` without scoping: `build_py` runs for installs
that never produce a wheel. **`bdist_wheel.run()`** is the reliable boundary.

#### Proposed `run()` sequence

```text
1. self.run_command('build')
      → build_py copies Python packages + package_data (*.ki) into build_lib
      → build_ext compiles koskari.lib into build_lib
2. ensure build_lib is on sys.path (import koskari.lib, koskari.serialize)
3. compile_kio_tree(build_lib)     # add sibling .kio beside each .ki
4. super().run()                   # zip build_lib into the wheel (.ki + .kio)
```

Step 3 **only adds files**. It does not delete, move, or filter out `.ki`.

PEP 517 **`build_wheel`** via `setuptools.build_meta` ultimately uses the same
`bdist_wheel` path when `setup.py` registers the cmdclass.

Wire in **`setup.py`**:

```python
cmdclass={
    'sdist': kos_sdist,
    'build_py': kos_build_py,
    'bdist_wheel': kos_bdist_wheel,
},
```

No change to **`pyproject.toml`** `build-system.requires` for koskari itself: the
compiler is the package being built. Requires only `setuptools` and `wheel`, plus
a successful **`build_ext`** in the same isolated env.

### Optional standalone command: `build_kio`

For debugging and `tox -e regen-kio` without building a full wheel, expose a
thin **`build_kio`** `setuptools.Command` that:

1. Requires finalized `build_py` + `build_ext`.
2. Calls the same **`compile_kio_tree(build_lib)`** helper.

`kos_bdist_wheel` calls that helper directly; `build_kio` is optional sugar.

### Compile helper location

**`tools/regen_kio.py`** (name illustrative):

* Pure orchestration; imports **`koskari.serialize.incremental`** and bootstrap
  lists from **`koskari.bootstrap`**.
* **`compile_kio_tree(root)`** — walk `root` for `*.ki` under package
  directories, or driven by an explicit manifest.
* **`compile_bootstrap_stream(root)`** — special case (see below).
* Usable from setuptools, Makefile, and CI without importing setuptools.

Keep compile logic **out of** `koskari.extras.setuptools` to avoid pulling
setuptools into runtime imports (same rule as the `metadata` / `plugin` split).

---

## Compile semantics

### Ordinary modules

One `.ki` file maps to one `.kio` file in the **same directory** under
`build_lib`:

```text
build/lib/koskari/cli/repl.ki  →  repl.kio
```

Use **`compile_ki_file_to_kio(ki_path, kio_path)`** from
`koskari.serialize.incremental`. Each top-level form is compiled and
**evaluated** during the compile pass (macros, `define-syntax`, etc.), matching
today's `load_module_demand` side effects for a single file.

Store **`source_filename`** in the `.kio` bodies as the logical `.ki` path (e.g.
`koskari/bootstrap/basics.ki`) so tracebacks and `linecache` resolve via the
sibling `.ki` shipped in the same wheel.

### Bootstrap stream (`koskari/bootstrap/*.ki`)

**Not** independent per-file compiles. `make_builtins_demand` uses **one**
accumulating `Module` and runs sources in **`_BUILTINS_SOURCES`** order; later
files depend on macros from earlier ones.

**Regen algorithm:**

```text
mod = new_module('koskari.builtins', ...)
for src in _BUILTINS_SOURCES:
    set mod __file__ to src path under build_lib
    read/compile/evaluate each top-level form into mod (shared namespace)
    write bodies to parallel.kio next to parallel.ki in build_lib
```

At runtime, **`make_builtins_demand`** (updated) loads each shard `.kio` in the
same order into the same accumulating module instead of `load_module_demand`
from `.ki`.

Alternative v1 simplification: one monolithic **`builtins.kio`** containing all
bodies in order. Sharded `.kio` matches source files and allows incremental
regen when one bootstrap file changes; pick sharded unless monolith is easier
for first landing.

### Prerequisites inside the wheel build env

| Step | Requirement |
| ---- | ----------- |
| `build_ext` | `koskari.lib` importable from `build_lib` |
| `compile_kio_tree` | Python compiler (`koskari.compile`), reader, bootstrap registry |
| First `new_module` | Triggers builtins — **chicken-and-egg** during bootstrap regen |

For **bootstrap shard compilation**, the driver must use the **same** Python
bootstrap compiler that `make_builtins_demand` uses today (compile from `.ki`
while building `.kio`). The wheel does not need pre-existing `.kio` to build
`.kio`.

---

## Loader preference: `.kio` over `.ki`

Today the import hook already registers **`.kio` before `.ki`** on
`KoskariFileFinder`, so a sibling `foo.kio` wins over `foo.ki` when both exist.

Gaps to close when both `.kio` and `.ki` are installed (wheel layout):

| Site | Current behaviour | Target |
| ---- | ----------------- | ------ |
| **`KoskariSourceFileLoader`** | Uses `.kio` when `get_filename` returns a `.kio` path | OK when finder picks `.kio` |
| **`get_source`** | Strips `.kio` → reads sibling `.ki` for linecache | OK when wheel ships both (additive policy) |
| **`make_builtins_demand`** | `load_module_demand` from `.ki` | Prefer `load_module_kio_demand` per bootstrap shard when `.kio` exists |
| **`load_module_demand` / `load_module_async`** | Always `source_open(.ki)` | If `__file__` ends with `.kio`, delegate to `load_module_kio_*` (importer already branches on extension for loader exec path; ensure **`__file__`** on installed modules points at `.kio`) |

### Fallback rule

```text
if exists(path.with_suffix('.kio')):
    load from .kio
elif exists(path.with_suffix('.ki')) or path.suffix == '.ki':
    load from .ki
else:
    ModuleNotFoundError
```

Apply consistently in:

* `koskari/importer.py` (finder / loader),
* `koskari/bootstrap/__init__.py` (`make_builtins_demand`, `make_builtins`),
* any direct `source_open` in `koskari/module.py` if `__file__` is `.ki` but
  `.kio` is present alongside.

### Environment overrides (optional)

| Variable | Effect |
| -------- | ------ |
| `KOSKARI_SOURCE=1` | Force `.ki` even when `.kio` exists (macro hacking). |
| `KOSKARI_NO_KIO=1` | Same as above; alias for clarity in docs. |

---

## Should `.kio` files live in `__pycache__`?

**Recommendation: no** for installed wheels and for the default layout.

### Why not `__pycache__`

| Concern | Detail |
| ------- | ------ |
| **Not Python bytecode** | `__pycache__` is defined for **PEP 3147** `.pyc` tied to `.py` imports. `.kio` is a Koskari codex cache with its own header, pool, and `META` section ([kio-format.md](kio-format.md)). |
| **Finder contract** | `KoskariPathFinder` discovers `*.kio` on `sys.path` next to package modules, same as `*.ki`. Nothing searches `__pycache__` for koskari artifacts today. |
| **Packaging** | Many manifests and tools treat `__pycache__` as disposable / excluded. Wheels that relied on `__pycache__` would fight setuptools `package_data` and user expectations. |
| **Naming** | PEP 3147 uses `module.cpython-39.pyc`. `.kio` has no parallel tag convention inside `__pycache__` without inventing a new scheme (`module.koskari-0.1.kio`?). |
| **Tracebacks** | `get_source` maps `.kio` → sibling `.ki`. A cache dir breaks the simple sibling mapping unless loader logic grows path-rewrite rules. |

### Recommended layout

**Wheels (production):** sibling **`module.kio`** added next to **`module.ki`**
in the package directory. Both ship in the wheel.

**Git / sdist:** only **`module.ki`** (no `.kio` in source or sdist tarball).

**Optional dev cache (future):** if editable installs should avoid rewriting the
source tree, use a **koskari-specific** directory, not `__pycache__`:

* e.g. **`.koskari/kio/`** at repo root, or
* **`$KOSKARI_CACHE_DIR`** with content-addressed names (`sha256(ki_bytes).kio`).

That cache is a **policy layer** on top of the loader fallback (check cache
after sibling `.kio`, before `.ki`). It is not required for wheel builds.

Soft invalidation metadata in the `.kio` **`META`** section
([kio-format.md §6](kio-format.md)) can record `source_digest` / `mtime` when
dev cache is implemented.

---

## Invalidation and errors

* **Hard invalidation:** `.kio` header magic / bytecode ABI mismatch → reject
  file ([kio-format.md](kio-format.md)).
* **Soft invalidation (future):** `META` digests vs source `.ki` on editable
  dev trees.
* **Load-time finalize failure:** treat as cache miss; if `.ki` available,
  recompile from source; else error.

Wheel builds should compile in CI/release so installed users never hit miss
paths unless they mix wheel koskari with hand-edited `.ki` overlays.

---

## Third-party `koskari_scripts` packages (future)

The koskari setuptools plugin already adds `*.ki` / `*.kio` to author
`package_data`. Authors may:

* ship `.ki` only (current),
* run the same compile hook in their own `bdist_wheel` if koskari exposes a
  reusable **`koskari.packaging.compile_kio_dist`** helper, or
* commit `.kio` for apps that prefer not to compile at wheel time.

Out of scope for koskari core v1 except documenting the pattern in
[api/koskari-scripts-packaging.md](../api/koskari-scripts-packaging.md) once
landed.

---

## Implementation phases

### Phase 1 — Driver + loader

1. **`tools/regen_kio.py`** with `compile_kio_tree` and bootstrap stream.
2. **`make_builtins_demand`** prefers `.kio` shards when present.
3. Importer / module path fallback audit (finder already prefers `.kio`).
4. Tests: parity (existing `test_kio_importer.py` pattern) for bootstrap shard
   load from `.kio`.

### Phase 2 — Wheel hook

1. **`kos_bdist_wheel`** in `setup.py`.
2. **`kos_sdist`** strips stray `.kio`.
3. Gitignore `**/*.kio`.
4. CI: build wheel, assert installed tree has **both** `.kio` and `.ki` under
   packaged koskari paths.

### Phase 3 — DX

1. `tox -e regen-kio` for local `build_lib` or inplace tree.
2. `make kio` target after `build_ext`.
3. Optional `KOSKARI_SOURCE` env for macro development.

---

## Open questions

1. **Monolithic vs sharded bootstrap `.kio`** — sharded recommended; monolith
   acceptable for first PR.
2. **Whether `uv build --wheel`** in `[testenv:build]` needs any flag changes —
   should work once `bdist_wheel` cmdclass is registered.

---

## Cross-references

* [kio-format.md](kio-format.md) — binary layout, META, loader algorithm
* [self-hosted-compiler-bootstrap.md](../self-hosted-compiler-bootstrap.md) —
  long-term bootstrap / stage0 / Pass B
* [api/import-system.md](../api/import-system.md) — module lifecycle, async vs
  demand load
* `koskari/serialize/incremental.py` — `compile_ki_file_to_kio`
* `koskari/bootstrap/__init__.py` — `_BUILTINS_SOURCES`, `make_builtins_demand`
* `setup.py` — `kos_build_py`, `kos_sdist` (extend with `kos_bdist_wheel`)

---

# The end.
