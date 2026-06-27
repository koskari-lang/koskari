# Koskari Feature Catalog

High-level inventory of what **koskari-ng** implements today, what lives in
bootstrap macros, and what remains planned from older sibilant lineages
(python-sibilant, sibilant-ceval). Authoritative detail:

- Compiler specials: [SPECIALS.md](SPECIALS.md)
- Bootstrap macros: [MACROS.md](MACROS.md)
- Opcodes: [OPCODES.md](OPCODES.md)
- Eval loop: [EVALUATOR.md](EVALUATOR.md)
- Open work: [TODO.md](TODO.md)

---

## Language (implemented)

### Special forms (`compile/*`)

Registered in `koskari/compile/specials/` and documented in [SPECIALS.md](SPECIALS.md).
Highlights (not an exhaustive list):

| Area | Implemented as |
| ---- | -------------- |
| Core | `compile/quote`, `compile/quasiquote`, `compile/unquote`, `compile/unquote-splicing`, `compile/quote-block`, `compile/quasiquote-block`, `compile/begin`, `compile/lambda`, `compile/function`, `compile/let`, `compile/define`, `compile/define-global`, `compile/setq`, `compile/delq`, `compile/refq`, `compile/bindq`, `compile/cons`, `compile/car`, `compile/cdr`, `compile/class` |
| Control | `compile/cond`, `compile/switch`, `compile/and`, `compile/or`, `compile/while`, `compile/for-each`, `compile/break`, `compile/continue`, `compile/try`, `compile/raise`, internal exception helpers |
| Context | `compile/with`, `compile/dyn-with`, `compile/dyn-wind`, `compile/parameterize` |
| Generators / async | `compile/yield`, `compile/yield-from`, `compile/await` |
| Calls / attrs / items | `compile/call`, `compile/call-attr`, `compile/attr`, `compile/set-attr`, `compile/inplace-attr`, `compile/del-attr`, `compile/ref-attr`, `compile/item`, `compile/set-item`, `compile/inplace-item`, `compile/del-item`, `compile/ref-item`, `compile/deref` |
| Values | `compile/define-values`, `compile/setq-values` |
| Import | `import`, `import-from` |
| Continuations | `compile/call/cc`, `compile/let/cc`, `compile/capture/cc`, `compile/reset`, `compile/shift` |
| Macros (lowering) | `compile/syntax-rules`, `compile/macro-rules`, `compile/syntax-match`, `compile/macro-match`, `compile/special-rules`, `compile/special-match` |
| Operators | `compile/+`, `-`, `*`, `/`, `//`, `///`, `%`, `**`, `@`, `&`, `|`, `^`, `~`, `<<`, `>>`, `compile/divmod`, comparisons (`compile/==`, `!=`, `<`, `<=`, `>`, `>=` and `equal?` / `lt?` aliases), `compile/is`, `compile/is-not`, `compile/in`, `compile/not-in`, **`compile/instance?`**, **`compile/subclass?`**, `compile/not`, `compile/repr`, `compile/truth`, `compile/iter`, `compile/aiter`, `compile/str`, `compile/format`, `compile/len`, `compile/type`, `compile/dir`, `compile/vars` |
| Other | `compile/bytecode`, `compile/inplace`, `compile/inplace-item`, `compile/inplace-attr`, `compile/globals`, `compile/locals`, `compile/gensym-thunk` (`{…}` thunk syntax) |

**Not compiler specials** (use macros or runtime instead): `compile/if`, `compile/when`, `compile/unless` (macros in `bootstrap/_conditionals.ki`); **`define-syntax`** / **`extend-syntax`** (macros in `bootstrap/_define_syntax.ki`); assignment sugar **`set!`**, **`ref`**, **`bind!`**, etc. ([MACROS.md](MACROS.md)).

### Operators and calls

- Variadic arithmetic/bitwise specials with **left-to-right constant folding** on leading constants ([SPECIALS.md](SPECIALS.md) § Object operator specials).
- **Chained** comparisons (`a < b < c`).
- Method calls: **`compile/call-attr`**, dot-prefix **`(compile/call .method obj …)`**, and **`(compile/call (attr obj m) …)`** expansion — no separate `method-call` special.

### Macros

- **Reader macros** on `koskari.reader.parser` (quote, quasiquote, unquote, unquote-splicing, block quasiquote, `&` → ref, thunk `{…}`, etc.).
- **Compile-time**: `syntax-rules`, `macro-rules`, `syntax-match`, `macro-match`, **`special-rules`**, **`special-match`**; hygiene via `koskari.syntax` / `koskari.hygiene` ([language/macro-hygiene-syntax-boxes.md](language/macro-hygiene-syntax-boxes.md), [MATCHER.md](MATCHER.md)).
- **Local compile-time binding**: **`let-syntax`**, **`inline-macro`** ([MACROS.md](MACROS.md); archived design [archive/let-syntax-and-inline-macro.md](archive/let-syntax-and-inline-macro.md)).
- **`macroexpand-1`** in `bootstrap/basics.ki`.

### Layout syntax (reader)

Indentation-based blocks; heads via `layout/<name>` in the module namespace.
Implemented handlers include:

`layout/if`, `layout/switch`, `layout/try`, `layout/while`, `layout/for-each`, `layout/dyn-wind`, `layout/class`, `layout/define-class`, `layout/call`, `layout/block`, `layout/block-begin`, `layout/quote`, `layout/quasiquote`, `layout/syntax-rules`, `layout/macro-rules`, `layout/special-rules`, `layout/syntax-match`, `layout/macro-match`, `layout/special-match`.

Unknown heads fall back to **`layout/block`**. See [LAYOUTS.md](LAYOUTS.md).

### Functions and formals

- **`compile/lambda`** (named and anonymous; optional return type and docstring).
- **`compile/function`** (named only; no closure self-ref at creation — used by
  `compile/define` / `compile/define-global` function shorthand).
- **`define-class`**, **`define-dataclass`**, **`define-coroutine`** (`coroutine-fn`), **`define-demanding`** (`demand` wrapper) — macros in bootstrap.
- Formals: positionals, defaults, `*:`, `**:`, keyword-only (see `gather_formals`).

### Async, generators, and `defer`

- **`compile/await`**: explicit await; ordinary Python awaitables are also **auto-awaited** at CALL boundaries unless the value is a **`defer`** handle.
- **`defer`**: invokes the wrapped call at **construction**, excludes the handle from auto-await; explicit await drives delegation ([api/defer-coroutine.md](api/defer-coroutine.md)).
- **`compile/yield`**, **`compile/yield-from`**; Koskari and Python async generators supported in the VM.
- **`with` / `dyn-with`**: sync and async context managers (VM auto-awaits enter/exit when needed).

### Dynamic parameters

- **`compile/parameterize`**, **`compile/define-parameter`** / **`parameterq`** macros, runtime **`ParamRef`** / **`parameter`**.

---

## Language (planned or historical only)

These appear in [language/special-forms.md](language/special-forms.md) or older notes but are **not** implemented as described in koskari-ng today:

| Feature | Notes |
| ------- | ----- |
| **`declare-async`** | Use **`define-coroutine`** + **`coroutine-fn`** instead |
| **`await-for-each`** | No dedicated form; async iteration uses normal **`for-each`** / **`compile/for-each`** under the async evaluator |
| **`await-with`** | No separate form; **`compile/with`** handles async managers |
| **`method-call` / `!`** | Call-attr and dot-prefix sugar |
| **`doc`** special | Docstrings via leading string on **`compile/lambda`** / **`compile/function`** / class body |
| **`build-tuple`**, **`build-list`**, … as specials | Runtime **`build_tuple`**, **`build_list`**, … in bootstrap ([api/builtins.md](api/builtins.md)) |
| Slices, fenced literals, bundles, match-case, … | [TODO.md](TODO.md) |

---

## Runtime and native layer (implemented)

### Types (`koskari.lib`)

`symbol`, `keyword`, `pair` / `nil`, `gensym`, namespaces (`Namespace`, `ModuleNamespace`, `CollectorNamespace`, …), `Ref` variants (`AttributeRef`, `ItemRef`, `DynamicRef`, `ConstantRef`, `MemoizedRef`, `ParamRef`), `Formals`, `values`, `Code`, `Function`, `Expression`, `Coroutine`, `AsyncGenerator`, continuations, **`defer`**, `Domain`, operand `Stack`, etc. See [api/types.md](api/types.md).

### Builtins (bootstrap + `koskari.lib`)

Loaded via **`koskari.bootstrap.make_builtins()`** / **`make_builtins_demand()`** from Python registry + **`koskari/bootstrap/*.ki`** (see [MACROS.md](MACROS.md)). Includes `cons`, `car`, `cdr`, collection builders, `apply`, `deref`, `defer`, `demand` / `demand-call`, reducing operators, and Python re-exports wired in **`make_bootstrap()`**.

### Bytecode and compiler

- **4-byte** instructions; constant pool; delta **position** table; **extent** table; **`mprotect`** read-only code after init.
- Pipeline: parse (reader) → expand macros → compile specials → **`Compiler.complete`** (including **`compress`** peephole, VAR→FAST). “Optimize” as a separate phase is not a distinct pass beyond this.
- Components: `koskari.compile` (`Compiler`, `Code`, fragments).

### Evaluator

- **`koskari_eval`** drive loop; boundary events **RETURN**, **RAISE**, **YIELD**, **AWAIT**; delimited **reset/shift** and undelimited **call/cc** ([interpreter/callcc.md](interpreter/callcc.md)).
- **Domain TLS** for ambient dynamic context and parameters.
- **GIL cooperation**: prologue reunion + RUN tail-dispatch poll (~every 2048 opcodes), **`PyErr_CheckSignals()`**, paired **`Py_BEGIN_ALLOW_THREADS` / `Py_END_ALLOW_THREADS`** when due ([EVALUATOR.md](EVALUATOR.md) §1.6).
- Flattened async-gen attach, eager-step awaitables, **YIELD_FROM** driver.

### Modules and import

- **`koskari.module`**: `new_module`, `load_module_async`, `load_module_demand`, …
- **`koskari.importer`**: `.ki` / `.kio` on `sys.meta_path`; **`exec_module`** defaults to demand path; **`KOSKARI_NOIMPORTER`** to disable auto-install.
- Compiler **`import`** / **`import-from`** → **IMPORT** / **IMPORT_REL** / **IMPORT_FROM** opcodes.

### Packaging: installed commands (`koskari_scripts`)

Third-party apps can ship pip-installable CLI commands whose sources stay in
**`.ki`** / **`.kio`** files — no per-app Python **`main`** shim or
**`asyncio.run`** wrapper.

| Piece | Role |
| ----- | ---- |
| **`koskari_scripts`** entry point group | Author declares **`name = module:callable`** (e.g. **`koslife = koslife.cli:main`**) |
| **`koskari.extras.setuptools`** | Build-time plugin (`metadata.py` + `plugin.py`): register **`install_scripts`** hook for koskari stub files, add **`*.ki`** / **`*.kio`** to **`package_data`** for author-declared **`packages=`**, set **`zip_safe = False`** |
| **`koskari.launcher`** | Runtime: **`run_resolved_script(spec, name)`** resolves one distribution's **`koskari_scripts`** metadata, runs target under **`drive_async_loop`** + async module load |
| **`koskari.importer.import_module_async`** | Async **`.ki`** / **`.kio`** import path for hosted scripts |

**Author metadata:** declare **`packages=`** explicitly; list **`koskari`** in
**`install_requires`** and **`build-system.requires`**. Prefer **`setup.cfg`**
(or **`pyproject.toml`**) plus a minimal **`setup.py`** shim calling
**`setup()`** — see **`examples/life`**.

**Setuptools ordering:** **`finalize_distribution_options`** runs during
**`Distribution.__init__`**, before **`setup.cfg`** is parsed. The plugin
therefore mirrors on **two paths**: the finalize hook (inline **`setup()`**
kwargs) and a lazy **`parse_config_files`** wrapper (config-file entry points).
Both invoke the same idempotent **`configure_koskari_scripts`**.

**Reference example:** **`examples/life`** (**`koslife`** command).
**User-facing recipe:** **`docs/patterns/recipes/packaging-installed-scripts.md`**.
**Archived design:** [archive/koskari-scripts-packaging.md](archive/koskari-scripts-packaging.md).

### Continuations (summary)

- **Delimited**: `compile/reset`, `compile/shift`; **INVOKE_K** / graft model ([interpreter/callcc.md](interpreter/callcc.md)).
- **Undelimited**: `compile/call/cc`, `compile/let/cc`, `compile/capture/cc`; Python invoke via **`KoskariContinuationApply`**.
- **Dynamic wind**: `compile/dyn-wind`; **PUSH_WIND** / **POP_WIND** on transfer.
- **Nested interrupt during transfer (done):** when a wind thunk invokes another
  continuation while a transfer plan is active, the outer plan is suspended on
  **`plan_stack`**, the inner transfer completes, then the outer is replanned via
  **`kos_build_direct_install_plan`**. See
  [archive/interrupted-transfer-plans.md](archive/interrupted-transfer-plans.md) §5;
  tests in **`TestDynWindContinuations`**.
- **Failed transfer abandonment (done):** on exception during a transfer step,
  abandon plan + **`plan_stack`** and dispatch via **`try_local`** on the
  manifested chain (in-place migration; no scratch-space transfer). Foreign
  cc-unwind sentinel re-emit is carved out (signal delivery, not failure). See
  [archive/interrupted-transfer-plans.md](archive/interrupted-transfer-plans.md) §4.

---

## Implementation checklist (native VM)

Items below are **done** in koskari-ng unless noted in [TODO.md](TODO.md).

**Types and refs** — atoms, pairs, namespaces, all ref kinds, formals, values, defer, stack, domain.

**Operators (C)** — reducing arithmetic/bitwise, chained compares, membership, `build-str`.

**Utilities (C)** — `apply`, `reapply`, `identity`, `constantly`, collection builders.

**Evaluator core** — `KosContext`, dynamic chain (`KosDynNode`), transfer plans
(including suspend/resume/replan on nested continuation invoke and abandon +
**`try_local`** on failed steps), wind/unwind, `koskari_eval`, coroutine/async-gen
wrappers, GC hooks.

**Bytecode** — `Code`, full opcode surface per [OPCODES.md](OPCODES.md) §1.6 (runtime implementations for all shipped opcodes except compile-time labels and **THEEND**).

**Continuation opcodes** — PUSH/POP PROMPT/SHIFT/CC, CAPTURE_CC, INVOKE_K, etc.

**Namespace opcodes** — PUSH_NS / POP_NS; **VAR_STORE**; **INVOKE_K_VAR** for shift-by-name patterns.

---

## Abandoned or deferred design choices

- CPython bytecode as a Koskari compilation target
- Wide bytecode format
- Alternate module representation (see [TODO.md](TODO.md) for active design threads)

---

## Related documentation

| Topic | Doc |
| ----- | --- |
| Special forms | [SPECIALS.md](SPECIALS.md) |
| Macros | [MACROS.md](MACROS.md) |
| Layout syntax | [LAYOUTS.md](LAYOUTS.md), [docs/reference/reader-layout.md](../docs/reference/reader-layout.md) |
| Planned language surface | [language/special-forms.md](language/special-forms.md), [language/async.md](language/async.md) |
| Import | [api/import-system.md](api/import-system.md) |
| Installed scripts (`koskari_scripts`) | [archive/koskari-scripts-packaging.md](archive/koskari-scripts-packaging.md) |
| GIL (future domain-TLS design) | [interpreter/gil-cooperation.md](interpreter/gil-cooperation.md) |
