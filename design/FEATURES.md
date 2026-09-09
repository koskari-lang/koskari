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
| Control | `compile/cond`, `compile/switch`, `compile/match`, `compile/and`, `compile/or`, `compile/while`, `compile/for-each`, `compile/break`, `compile/continue`, `compile/try`, `compile/raise`, internal exception helpers |
| Context | `compile/with`, `compile/dyn-with`, `compile/dyn-wind`, `compile/parameterize` |
| Generators / async | `compile/yield`, `compile/yield-from`, `compile/await` |
| Calls / attrs / items | `compile/call`, `compile/call-attr`, `compile/attr`, `compile/set-attr`, `compile/inplace-attr`, `compile/del-attr`, `compile/ref-attr`, `compile/item`, `compile/set-item`, `compile/inplace-item`, `compile/del-item`, `compile/ref-item`, `compile/deref` |
| Values | `compile/define-values`, `compile/setq-values` |
| Import | `import`, `import-from`, `import-all-from` |
| Continuations | `compile/call/cc`, `compile/let/cc`, `compile/capture/cc`, `compile/reset`, `compile/shift` |
| Macros (lowering) | `compile/syntax-rules`, `compile/macro-rules`, `compile/syntax-match`, `compile/macro-match`, `compile/special-rules`, `compile/special-match` |
| Operators | `compile/+`, `-`, `*`, `/`, `//`, `///`, `%`, `**`, `@`, `&`, `|`, `^`, `~`, `<<`, `>>`, `compile/divmod`, comparisons (`compile/==`, `!=`, `<`, `<=`, `>`, `>=` and `equal?` / `lt?` aliases), `compile/is`, `compile/is-not`, `compile/in`, `compile/not-in`, **`compile/instance?`**, **`compile/subclass?`**, `compile/not`, `compile/repr`, `compile/truth`, `compile/iter`, `compile/next`, `compile/str`, `compile/format`, `compile/len`, `compile/type`, `compile/dir`, `compile/vars` |
| Fences | `compile/apply-fence` (see § Fenced foreign literals) |
| Other | `compile/bytecode`, `compile/inplace`, `compile/inplace-item`, `compile/inplace-attr`, `compile/globals`, `compile/locals`, `compile/gensym-thunk` (`{…}` thunk syntax) |

**Not compiler specials** (use macros or runtime instead): `compile/if`, `compile/when`, `compile/unless` (macros in `bootstrap/_conditionals.ki`); **`define-syntax`** / **`extend-syntax`** (macros in `bootstrap/_define_syntax.ki`); assignment sugar **`set!`**, **`ref`**, **`bind!`**, etc. ([MACROS.md](MACROS.md)).

### Operators and calls

- Variadic arithmetic/bitwise specials with **left-to-right constant folding** on leading constants ([SPECIALS.md](SPECIALS.md) § Object operator specials).
- **Chained** comparisons (`a < b < c`).
- Method calls: **`compile/call-attr`**, dot-prefix **`(compile/call .method obj …)`**, and **`(compile/call (attr obj m) …)`** expansion — no separate `method-call` special.
- **Bracket-pair type and call convention**: `[ ]` is **not** a vector literal — it reads into a `bracket-pair` (a value-equal `pair` subtype that marks `[ ]` origin) and **compiles identically to `( )` by default** (a non-empty head-position list is a call either way). The `( )` = call / `[ ]` = data split is a convention; optional module pragmas turn it into a check — `reader/brackets` (reader: emit the marker) and `compiler/call-brackets` (compiler: `ignore` / `warn` / `error` when a bracket-pair reaches call position). See [SYNTAX.md](SYNTAX.md), [PRAGMA.md](PRAGMA.md).

### Runtime match/case

Value-level pattern matching over the native matcher engine
(`koskari.lib.Matcher`), the same engine that drives macro and special-form
dispatch. **`compile/match`** (s-expr) and **`layout/match`** (block syntax)
lower each arm's pattern to a frozen matcher constant and emit a
`SWITCH_EQ`-style chain of **`MATCH_CASE`** opcodes, each binding its arm's
captures on a match, with a per-site `KOS_CACHE_MATCHER` slot (thaw-once
matcher plus the capture slot indices).

- **Case-pattern dialect**: capture symbols, literals, `x :: T` type
  constraints, sequence `#[…]` / `#(…)` (with `*:` rest), mapping `#{K: PAT}`,
  class patterns `Point[x y]` (PEP 634 field rules: `__match_args__`
  positional, `k: pat` keyword), `(app f PAT)` projection, `(? pred)`
  predicates, `(or …)` / `(not …)`, and registry-head passthrough. Pattern
  kind is decided by head symbol or marker, never by bracket type.
- **Guards**: `when:` / `unless:` on an arm; subject survives a failed guard
  and falls to the next arm.
- **Semantics**: first arm wins; bindings land in the enclosing scope and
  persist after the form (Python-consistent); no match + no `else:` → `None`;
  strings/bytes are atoms to sequence patterns. On 3.9 the engine synthesizes
  `__match_args__` for dataclasses and namedtuples.

See [MATCHER.md](MATCHER.md) § Runtime match/case;
`koskari/compile/specials/conditionals.py`,
`koskari/reader/layouts/conditionals.py`, `koskari/lib/opcode/MATCH_CASE.c`.

### Slices

Bootstrap macros in `bootstrap/_iter.ki`, not compiler specials:
**`compile/slice`** routes `(slice …)` to the runtime **`build-slice`**
constructor (keyword `start:` / `stop:` / `step:`), and **`compile/item-slice`**
gives the idiomatic direct form `(item-slice seq 1)` in place of
`(item seq (slice 1 None))`. See
[docs/reference/concepts/idiomatic-koskari.md](../docs/reference/concepts/idiomatic-koskari.md),
[docs/reference/runtime/build-slice.md](../docs/reference/runtime/build-slice.md).

### Macros

- **Reader macros** on `koskari.reader.parser` (quote, quasiquote, unquote, unquote-splicing, block quasiquote, `&` → ref, thunk `{…}`, etc.).
- **Compile-time**: `syntax-rules`, `macro-rules`, `syntax-match`, `macro-match`, **`special-rules`**, **`special-match`**; hygiene via `koskari.syntax` / `koskari.hygiene` ([archive/macro-hygiene-syntax-boxes.md](archive/macro-hygiene-syntax-boxes.md), [MATCHER.md](MATCHER.md)).
- **Local compile-time binding**: **`let-syntax`**, **`inline-macro`** ([MACROS.md](MACROS.md); archived design [archive/let-syntax-and-inline-macro.md](archive/let-syntax-and-inline-macro.md)).
- **`macroexpand-1`** in `bootstrap/basics.ki`.

### Layout syntax (reader)

Indentation-based blocks; heads via `layout/<name>` in the module namespace.
Implemented handlers include:

`layout/if`, `layout/switch`, `layout/match`, `layout/try`, `layout/while`, `layout/for-each`, `layout/dyn-wind`, `layout/class`, `layout/define-class`, `layout/call`, `layout/block`, `layout/block-begin`, `layout/quote`, `layout/quasiquote`, `layout/syntax-rules`, `layout/macro-rules`, `layout/special-rules`, `layout/syntax-match`, `layout/macro-match`, `layout/special-match`.

Unknown heads fall back to **`layout/block`**. See [LAYOUTS.md](LAYOUTS.md).

### Fenced foreign literals

A triple-backtick fence captures raw source in a declared foreign format; the
reader lowers it to **`(compile/apply-fence NAME RAW META)`**, and the
**`compile/apply-fence`** special resolves **`fence/<name>`** in the module
namespace and compiles the Koskari source that handler returns. The transform
is compile-time, parallel to layout expansion. **`fence/bash`** (lowering to
`run-process`) ships as the sole built-in handler; `koskari/fences/`. A JSON
fence was prototyped and dropped. See
[archive/fenced-foreign-literal.md](archive/fenced-foreign-literal.md).

### Functions and formals

- **`compile/lambda`** (named and anonymous; optional return type and docstring).
- **`compile/function`** (named only; no closure self-ref at creation — used by
  `compile/define` / `compile/define-global` function shorthand).
- **`define-class`**, **`define-dataclass`**, **`define-coroutine`** (`coroutine-fn`), **`define-demanding`** (`demand` wrapper) — macros in bootstrap.
- Formals: positionals, defaults, `*:`, `**:`, keyword-only (see `gather_formals`).
- **By-ref formals**: a positional or keyword formal prefixed with `&` (e.g.
  `[a &result]`) aliases the caller-supplied `Ref` as the slot's storage, so
  `set!` in the callee writes through to the caller's binding. Not permitted on
  `*` / `**`. See [archive/by-ref-formals.md](archive/by-ref-formals.md).

### Async, generators, and `defer`

- **`compile/await`**: explicit await; ordinary Python awaitables are also **auto-awaited** at CALL boundaries unless the value is a **`defer`** handle or passes asyncio's **isfuture** duck check (`_asyncio_future_blocking`) -- futures and Tasks are handles, resolved only by explicit await.
- **`defer`**: invokes the wrapped call at **construction**, excludes the handle from auto-await; carries the full coroutine protocol (passes `asyncio.iscoroutine`, drivable by Tasks), and driving to completion consumes it -- reuse raises `RuntimeError` like a finished coroutine ([api/defer-coroutine.md](api/defer-coroutine.md)).
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
| Bundles, … | [TODO.md](TODO.md) |

---

## Runtime and native layer (implemented)

### Types (`koskari.lib`)

`symbol`, `keyword`, `pair` / `nil`, `gensym`, namespaces (`Namespace`, `ModuleNamespace`, `CollectorNamespace`, …), `Ref` variants (`AttributeRef`, `ItemRef`, `DynamicRef`, `ConstantRef`, `MemoizedRef`, `ParamRef`), `Formals`, `values`, `Code`, `Arena`, `Function`, `Expression`, `Coroutine`, `AsyncGenerator`, continuations, **`defer`**, `Domain`, `Matcher`, operand `Stack`, etc. See [api/types.md](api/types.md).

### Builtins (bootstrap + `koskari.lib`)

Loaded via **`koskari.bootstrap.make_builtins()`** / **`make_builtins_demand()`** from Python registry + **`koskari/bootstrap/*.ki`** (see [MACROS.md](MACROS.md)). Includes `cons`, `car`, `cdr`, collection builders, `apply`, `deref`, `defer`, `demand` / `demand-call`, reducing operators, and Python re-exports wired in **`make_bootstrap()`**.

### Bytecode and compiler

- **4-byte** instructions; constant pool; delta **position** table; **extent** table — all in one unified block per `Code`.
- **Sealing and arena packing** ([PRAGMA.md](PRAGMA.md); archived design: [archive/code-object-arena-packing.md](archive/code-object-arena-packing.md)): `compiler/protect-code` (baseline off) seals each block page-aligned **`mprotect(PROT_READ)`** at construction; `compiler/pack-module` (baseline on) relocates a module's durable code into a shared, singly-sealed **`Arena`** at module seal (`Compiler.seal()`, weakref registry over `Compiler.complete`). Default loads run off/on: compact, read-only at rest. Loaders take `seal=` / `detach=` completion flags; the bootstrap builtins seal into a single arena.
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
- Compiler **`import`** / **`import-from`** / **`import-all-from`** → self-contained **IMPORT** / **IMPORT_FROM** / **IMPORT_STAR** opcodes.

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
- **Transfer goals (done):** a continuation invoke arms a goal recording
  where control is going; the prologue re-derives each step from the live
  chain, parks while a thunk or `__exit__` runs, nests when a thunk invokes
  another continuation, and frees every goal on a failed step before the
  raise walks the chain as it stands. See [EVALUATOR.md §6](EVALUATOR.md)
  and [interpreter/transfer-plan-rework.md](interpreter/transfer-plan-rework.md);
  tests in **`TestDynWindContinuations`** and **`TestDelimitedTransferNested`**.
- **Recursion limit (done):** frame depth per context is bounded by
  `sys.getrecursionlimit()`; an install, a continuation landing, or a
  generator resume that would exceed it raises `RecursionError` into the
  calling frame. See [EVALUATOR.md §7.7](EVALUATOR.md).

---

## Implementation checklist (native VM)

Items below are **done** in koskari-ng unless noted in [TODO.md](TODO.md).

**Types and refs** — atoms, pairs, namespaces, all ref kinds, formals, values, defer, stack, domain.

**Operators (C)** — reducing arithmetic/bitwise, chained compares, membership.

**Utilities (C)** — `apply`, `reapply`, `identity`, `constantly`, collection builders, `build_str`.

**Evaluator core** -- `KosContext`, dynamic chain (`KosDynNode`), transfer goals
(parked across thunks and awaits, nested on a thunk's invoke, freed on a
failed step before **`try_local`**), the extent contract and obligations, `koskari_eval`, coroutine/async-gen
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
| Matcher and match/case | [MATCHER.md](MATCHER.md) |
| Macros | [MACROS.md](MACROS.md) |
| Layout syntax | [LAYOUTS.md](LAYOUTS.md), [docs/reference/reader-layout.md](../docs/reference/reader-layout.md) |
| Planned language surface | [language/special-forms.md](language/special-forms.md), [language/async.md](language/async.md) |
| Import | [api/import-system.md](api/import-system.md) |
| Installed scripts (`koskari_scripts`) | [archive/koskari-scripts-packaging.md](archive/koskari-scripts-packaging.md) |
| GIL (future domain-TLS design) | [archive/gil-cooperation.md](archive/gil-cooperation.md) |
