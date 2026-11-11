# Koskari Feature Catalog

High-level inventory of what **Koskari** implements today, what lives in
bootstrap macros, and what remains planned from older sibilant lineages
(python-sibilant, sibilant-ceval). Authoritative detail:

- Compiler specials: [SPECIALS.md](SPECIALS.md)
- Bootstrap macros: [MACROS.md](MACROS.md)
- Opcodes: [OPCODES.md](OPCODES.md)
- Eval loop: [EVALUATOR.md](EVALUATOR.md)
- Open work: [TODO.md](TODO.md)

---

## Language (implemented)

### Special forms

Registered in `koskari/compile/specials/` and documented in [SPECIALS.md](SPECIALS.md).
Highlights (not an exhaustive list):

| Area | Implemented as |
| ---- | -------------- |
| Core | `quote`, `quasiquote`, `quote-syntax`, `quasiquote-syntax`, `bare-syntax`, `unquote`, `unquote-splicing`, `begin`, `lambda`, `function`, `let`, `letrec`, `var`, `global`, `define`, `define-global`, `setq`, `delq`, `refq`, `bindq`, `takeq`, `cons`, `car`, `cdr`, `nil?`, `build-class` |
| Control | `cond`, `switch`, `match`, `and`, `or`, `while`, `for-each`, `break`, `continue`, `return`, `try`, `raise`, internal exception helpers |
| Context | `with`, `dyn-with`, `dyn-wind`, `parameter`, `parameterize` |
| Generators / async | `yield`, `yield-from`, `await` |
| Calls / attrs / items | `call`, `call-attr`, `attr`, `set-attr`, `inplace-attr`, `del-attr`, `ref-attr`, `take-attr`, `item`, `set-item`, `inplace-item`, `del-item`, `ref-item`, `take-item`, `deref` |
| Values | `define-values`, `setq-values` |
| Collections | `#tuple`, `#list`, `#list-append`, `bracket-cons`, `bracket-proper` |
| Import | `import`, `import-from`, `import-all-from`, `use` |
| Continuations | `call/cc`, `let/cc`, `capture/cc`, `reset`, `shift`, `reset-at`, `shift-at` |
| Macros (lowering) | `syntax-rules`, `macro-rules`, `syntax-match`, `macro-match`, `special-rules`, `special-match` |
| Operators | `+`, `-`, `*`, `/`, `//`, `%`, `**`, `@`, `&`, `|`, `^`, `~`, `<<`, `>>`, `divmod`, comparisons (`==`, `!=`, `<`, `<=`, `>`, `>=`, `in`, `not-in`, `is`, `is-not`, aliased `equal?`, `not-equal?`, `lt?`, `le?`, `gt?`, `ge?`, `in?`, `not-in?`, `is?`, `is-not?`), **`instance?`**, **`subclass?`**, `not`, `repr`, `truth`, `iter`, `next`, `#str`, `format`, `len`, `type`, `dir`, `vars` (zero operands: the frame namespace) |
| Fences | `apply-fence` (see § Fenced foreign literals) |
| Other | `bytecode`, `inplace`, `globals`, `gensym-thunk` (`{…}` thunk syntax) |

**Not compiler specials** (use macros or runtime instead): `if`, `when`, `unless` (macros in `bootstrap/_conditionals.ki`); **`str`** (macro in `bootstrap/_functions.ki`; one argument lowers to the `#str` special); **`define-syntax`** / **`extend-syntax`**, and the **`syntax`**, **`def`** and **`class`** binding macros (`bootstrap/_define_syntax.ki`); assignment sugar **`set!`**, **`ref`**, **`bind!`**, etc. ([MACROS.md](MACROS.md)).

### Operators and calls

- Variadic **`+ - * / // & | ^ @`** reduce left to right and **fold leading constants** into one CONST, stopping at the first non-constant and leaving any result past CPython's optimizer caps (128-bit int, 4096-char str, 256-element sequence) to the runtime. **`**`** takes base, exponent and an optional modulus, without folding; **`%`**, **`<<`**, **`>>`**, **`divmod`** are binary ([SPECIALS.md](SPECIALS.md) § Object operator specials).
- **Chained** comparisons (`a < b < c`).
- Method calls: **`call-attr`**, dot-prefix **`(call .method obj …)`**, and **`(call (attr obj m) …)`** expansion — no separate `method-call` special.
- **Bracket-pair type and call convention**: `[ ]` is **not** a vector literal — it reads into a `bracket-pair` (a value-equal `pair` subtype that marks `[ ]` origin) and compiles to the same code as `( )`. The `( )` = call / `[ ]` = data split is a convention that two module pragmas check: `reader/brackets` (reader: emit the marker, baseline on) and `compiler/call-brackets` (compiler: `ignore` / `warn` / `error` when a bracket-pair reaches call position, baseline `warn`), so a `[ ]` call warns by default. See [SYNTAX.md](SYNTAX.md), [PRAGMA.md](PRAGMA.md).

### Runtime match/case

Value-level pattern matching through the matcher compiler
(`koskari.matcher.emitter`), the same lowering that drives macro and
special-form dispatch. **`match`** (s-expr) and **`layout/match`**
(block syntax) lower each arm's pattern to its **`MATCH_*`** instructions
inline, a chain of arms each jumping to the next on a miss, the captures
stored into the enclosing function's locals as the pattern walks.

- **Case-pattern dialect**: capture symbols, literals, `x :: T` type
  constraints, sequence `#[…]` / `#(…)` (with `*:` rest), mapping `#{K: PAT}`,
  class patterns `Point[x y]` (PEP 634 field rules: `__match_args__`
  positional, `k: pat` keyword), `(app f PAT)` projection, `(? pred)`
  predicates, `(or …)` / `(not …)`, and registry-head passthrough. Pattern
  kind is decided by head symbol or marker, never by bracket type.
- **Guards**: `when:` / `unless:` on an arm; subject survives a failed guard
  and falls to the next arm.
- **Semantics**: first arm wins; bindings go into the enclosing scope and
  persist after the form (Python-consistent); no match + no `else:` → `None`;
  strings/bytes are atoms to sequence patterns. On 3.9 the class test
  synthesizes `__match_args__` for dataclasses and namedtuples. A predicate
  or extractor is a call from the matching frame and flattens.

See [MATCHER.md](MATCHER.md) § Runtime match/case and
[archive/matcher-bytecode.md](archive/matcher-bytecode.md);
`koskari/compile/specials/match.py`, `koskari/matcher/emitter.py`,
`koskari/reader/layouts/conditionals.py`.

### Slices

Bootstrap macros in `bootstrap/_iter.ki`, not compiler specials:
**`slice`** routes `(slice …)` to the runtime **`build-slice`**
constructor (keyword `start:` / `stop:` / `step:`), and **`item-slice`**
gives the idiomatic direct form `(item-slice seq 1)` in place of
`(item seq (slice 1 None))`. See
[docs/guide/idiomatic-koskari.md](../docs/guide/idiomatic-koskari.md),
[docs/reference/runtime/build-slice.md](../docs/reference/runtime/build-slice.md).

### Macros

- **Reader macros** on `koskari.reader.parser`: `( )`, `[ ]` (bracket-pair), thunk `{…}`, strings with `f` / `r` / `b` prefixes, `;` comments, `'` quote, `` ` `` quasiquote, `,` / `,@` unquote inside a quasiquote, `#` collection openers (`#[`, `#(`, `#{`), `#'` / `` #` `` syntax quotes, `-'` datumq, `&` ref call, unary `-` / `+` / `~` (`reader/unary`), and triple-backtick fences. Suffix macros apply to a value just read: `[` index brackets (`reader/index-brackets`), `.` attributes (`reader/dot-attrs`) and `-` string segments (`reader/string-segments`).
- **Compile-time**: `syntax-rules`, `macro-rules`, `syntax-match`, `macro-match`, **`special-rules`**, **`special-match`**; hygiene via `koskari.syntax` / `koskari.hygiene` ([archive/macro-hygiene-syntax-boxes.md](archive/macro-hygiene-syntax-boxes.md), [MATCHER.md](MATCHER.md)).
- **Local compile-time binding**: **`let-syntax`**, **`inline-macro`** ([MACROS.md](MACROS.md); archived design [archive/let-syntax-and-inline-macro.md](archive/let-syntax-and-inline-macro.md)).
- **`macroexpand-1`** in `bootstrap/basics.ki`.
- **Other bootstrap macros**: `flet`, `labels`, `decorate`, `function-compose`, `set!`, `del!`, `take!`, `incr!` / `decr!`, `let*`, `let-values`, `define-environ`, `item*` / `attr*`, `global` / `local`, and more ([MACROS.md](MACROS.md)).

### Pragmas

**`pragma`** sets or reads settings on the module a form is written in; **`package-pragma`** also records the result as the default sub-packages and sub-modules inherit; **`active-pragma`** reads the pragma of the module being compiled (inside a macro body, the consumer's); **`help-pragma-key`** pages the help for one setting. Settings fall into the `reader/*`, `compiler/*` and `pragma/*` families. See [PRAGMA.md](PRAGMA.md).

### Iteration

**`map`**, **`filter`** and **`zip`** (builtins) and the **`koskari.itertools`** combinators `accumulate`, `batched`, `chain`, `chain-from-iterable`, `compress`, `cycle`, `dropwhile`, `filterfalse`, `islice`, `pairwise`, `starmap`, `takewhile` and `zip-longest` are lazy native iterators. A source may be a sync or async iterable, and a callable argument runs in the evaluator. **`groupby`** is written in Koskari in `koskari/itertools.ki`.

### Layout syntax (reader)

Indentation-based blocks; heads via `layout/<name>` in the module namespace.
Implemented handlers include:

`layout/if`, `layout/switch`, `layout/match`, `layout/try`, `layout/while`, `layout/for-each`, `layout/dyn-wind`, `layout/class`, `layout/build-class`, `layout/block`, `layout/block-begin`, `layout/inline-block`, `layout/import-from`, `layout/quote`, `layout/quasiquote`, `layout/quote-syntax`, `layout/quasiquote-syntax`, `layout/bare-syntax`, `layout/syntax-rules`, `layout/macro-rules`, `layout/special-rules`, `layout/syntax-match`, `layout/macro-match`, `layout/special-match`.

Unknown heads fall back to **`layout/block`**. See [LAYOUTS.md](LAYOUTS.md).

### Fenced foreign literals

A triple-backtick fence captures raw source in a declared foreign format; the
reader lowers it to **`(apply-fence NAME RAW META)`**, and the
**`apply-fence`** special resolves **`fence/<name>`** in the module
namespace and compiles the Koskari source that handler returns. The transform
is compile-time, parallel to layout expansion. Handlers are declared with the bootstrap
**`define-fence`** macro, which wraps the body in a `FenceHandler`
(`koskari/fences/`). **`fence/bash`** (`bootstrap/_fences.ki`) ships as the
sole built-in handler: it lowers to a function that runs the raw text under
`bash -c` with its arguments as `$1…`, or with `immediate: True` to an
in-place `run-process` call. A JSON
fence was prototyped and dropped. See
[archive/fenced-foreign-literal.md](archive/fenced-foreign-literal.md).

### Functions and formals

- **`lambda`** (named and anonymous; optional return type and docstring).
- **`function`** (named only; no closure self-ref at creation — used by
  `def` and the Scheme `(define (NAME FORMALS ...) BODY ...)` arm).
- **`def`** (`(var NAME (function NAME ...))`) and **`class`** (`(var NAME (build-class NAME ...))`) — binding macros in bootstrap; **`global def`** / **`global class`** bind at the module. Coroutine and demanding functions use **`@coroutine-fn`** / **`@demand`** over **`def`**.
- Formals: positionals, defaults, `*:`, `**:`, keyword-only (see `gather_formals`).
- **By-ref formals**: a positional or keyword formal prefixed with `&` (e.g.
  `[a &result]`) aliases the caller-supplied `Ref` as the slot's storage, so
  `set!` in the callee writes through to the caller's binding. Not permitted on
  `*` / `**`. See [archive/by-ref-formals.md](archive/by-ref-formals.md).

### Async, generators, and `defer`

- **`await`**: explicit await; ordinary Python awaitables are also **auto-awaited** at CALL boundaries unless the value is a **`defer`** handle or passes asyncio's **isfuture** duck check (`_asyncio_future_blocking`) -- futures and Tasks are handles, resolved only by explicit await.
- **`defer`**: invokes the wrapped call at **construction**, excludes the handle from auto-await; carries the full coroutine protocol (passes `asyncio.iscoroutine`, drivable by Tasks), and driving to completion consumes it -- reuse raises `RuntimeError` like a finished coroutine ([api/defer-coroutine.md](api/defer-coroutine.md)).
- **`yield`**, **`yield-from`**; Koskari and Python async generators supported in the VM.
- **`with` / `dyn-with`**: sync and async context managers (VM auto-awaits enter/exit when needed).
- **`create-task`**: schedules any awaitable and returns the Task, a handle excluded from auto-await; passing a `defer` consumes it.
- **`run-process`**: runs an argv in a subprocess and returns a **`ProcessResult`** (`koskari.asyncio`); a non-zero exit status is recorded on the result, not raised.

### Dynamic parameters

- **`parameterize`** and **`parameter`** specials, **`define-parameter`** / **`parameterq`** macros, runtime **`ParamRef`** and the **`parameter`** function.

---

## Language (planned or historical only)

These appear in [language/special-forms.md](language/special-forms.md) or older notes but are **not** implemented as described in Koskari today:

| Feature | Notes |
| ------- | ----- |
| **`declare-async`** | Use **`@coroutine-fn`** over **`def`** instead |
| **`await-for-each`** | No dedicated form; async iteration uses normal **`for-each`** under the async evaluator |
| **`await-with`** | No separate form; **`with`** handles async managers |
| **`method-call` / `!`** | Call-attr and dot-prefix sugar |
| **`doc`** special | Docstrings via leading string on **`lambda`** / **`function`** / class body |
| **`build-tuple`**, **`build-list`**, … as specials | Runtime **`build_tuple`**, **`build_list`**, … in bootstrap ([api/builtins.md](api/builtins.md)) |
| Bundles, … | [TODO.md](TODO.md) |

---

## Runtime and native layer (implemented)

### Types (`koskari.lib`)

`symbol`, `keyword`, `pair` / `nil`, `gensym`, namespaces (`Namespace`, `ModuleNamespace`, `CollectorNamespace`, …), `Ref` variants (`AttributeRef`, `ItemRef`, `DynamicRef`, `ConstantRef`, `MemoizedRef`, `ParamRef`), `Formals`, `values`, `Code`, `Arena`, `Function`, `Expression`, `Coroutine`, `AsyncGenerator`, continuations, **`defer`**, `Domain`, `Datum` / `datum` (a source form with its position) and `DatumFrame` (the matcher's captures), the lazy iterator types behind § Iteration, etc. See [api/types.md](api/types.md).

### Builtins (bootstrap + `koskari.lib`)

Loaded via **`koskari.bootstrap.make_builtins()`** / **`make_builtins_demand()`** from Python registry + **`koskari/bootstrap/*.ki`** (see [MACROS.md](MACROS.md)). Includes `cons`, `car`, `cdr`, collection builders, `apply`, `deref`, `defer`, `demand` / `demand-call`, reducing operators, and Python re-exports wired in **`make_bootstrap()`**.

### Bytecode and compiler

- **32-byte** instruction records (handler pointer and resolved operand beside opcode and arg); constant pool; delta **position** table; **extent** table — all in one unified block per `Code`.
- **Sealing and arena placement** ([PRAGMA.md](PRAGMA.md); archived design of the earlier relocating model: [archive/code-object-arena-packing.md](archive/code-object-arena-packing.md)): `compiler/protect-code` (baseline off) seals each block not placed in an arena page-aligned **`mprotect(PROT_READ)`** at construction; `compiler/pack-module` (baseline on) places function code at birth into the load's **`CodeAllocator`** chunks (**`Arena`**, whole units of sixteen system pages, first-fit, never moved) and `Compiler.seal()` trims every chunk to the pages it fills and seals it when the load completes. Top-level expressions are always heap. Loaders take `seal=` / `detach=` completion flags; the bootstrap builtins share one allocator across their files and seal once. A `.kio` records placement per function (`CodeFlags.ARENA`) and the importer decodes into a fresh allocator sealed before the first body runs.
- Pipeline: parse (reader) → expand macros → compile specials → **`Compiler.complete`** (including **`compress`** peephole, VAR→FAST). “Optimize” as a separate phase is not a distinct pass beyond this.
- Components: `koskari.compile` (`Compiler`, `Code`, fragments).
- **Parameters**: `parameterize` compiles to **PUSH_PARAM** / **POP_PARAM** around its body; `parameter` reads with **PARAM**.
- **Electors**: a handler chosen once for a site or an extent and tail-called on every later execution. The elected body lives in a cache slot on the code object or a field on a dyn node; the instruction record never changes. See [interpreter/electors.md](interpreter/electors.md), [JARGON.md](JARGON.md).

### Evaluator

- **`koskari_eval`** eval loop; it returns at the boundary events **RETURN**, **RAISE**, **YIELD**, **AWAIT**, while handler results such as reprologue, dispatch, poll and resume stay inside the loop; delimited **reset/shift** and undelimited **call/cc** ([EVALUATOR.md](EVALUATOR.md) §6).
- **Per-thread record** holding the ambient context stack, for ambient dynamic context and parameters. The domain answering on a thread is the active context's, else the innermost `with Domain():` scope's, else the thread default domain.
- **GIL cooperation**: the cooperation points (`CONTINUE`, frame install) test CPython's eval breaker and tail into **`kos_eval_poll`** when it is up. The poll releases the GIL with paired **`Py_BEGIN_ALLOW_THREADS` / `Py_END_ALLOW_THREADS`** when another thread asks, then runs **`Py_MakePendingCalls()`** and, on 3.12+, **`PyErr_CheckSignals()`**; the prologue polls in place ([EVALUATOR.md](EVALUATOR.md) §1.6).
- Flattened async-gen attach, eager-step awaitables, **YIELD_FROM** driver.

### Modules and import

- **`koskari.module`**: `new_module`, `load_module_async`, `load_module_demand`, …
- **`koskari.importer`**: `.ki` / `.kio` on `sys.meta_path`; **`exec_module`** defaults to demand path; **`KOSKARI_NOIMPORTER`** to disable auto-install.
- Compiler **`import`** / **`import-from`** / **`import-all-from`** → self-contained **IMPORT** / **IMPORT_FROM** / **IMPORT_STAR** opcodes.
- **`use`**: module-level import of whole features; each stem brings its runtime binding, `compile/` and `layout/` facets together under one bind stem (**IMPORT_USE**), and with no stems it lowers to **IMPORT_STAR**. On a Python module it behaves as `import-from` / `import-all-from`.
- **Program entry**: the **`entry-point`** decorator arm binds the decorated function as the module's **`__entry__`**; `koskari -m` and a bare-module `koskari_scripts` entry call it with no arguments. **`when-main`** remains a macro that runs its body when `__name__` is `"__main__"`.

### Packaging: installed commands (`koskari_scripts`)

Third-party apps can ship pip-installable CLI commands whose sources stay in
**`.ki`** / **`.kio`** files — no per-app Python **`main`** shim or
**`asyncio.run`** wrapper.

| Piece | Role |
| ----- | ---- |
| **`koskari_scripts`** entry point group | Author declares **`name = module:callable`** (e.g. **`koslife = koslife.cli:main`**), or a bare **`name = module`** to run the module's **`__entry__`** |
| **`koskari.extras.setuptools`** | Build-time plugin (`metadata.py` + `plugin.py` + `commands.py`): register **`KoskariInstallScripts`** (writes koskari stub files, into a wheel's `.data/scripts/` as well) and, where setuptools has it, **`KoskariEditableWheel`** for PEP 660 editable installs; add **`*.ki`** / **`*.kio`** to **`package_data`** for author-declared **`packages=`**, set **`zip_safe = False`** |
| **`koskari.launcher`** | Runtime: **`run_resolved_script(spec, name, argv=None)`** resolves one distribution's **`koskari_scripts`** metadata, runs target under **`drive_async_loop`** + async module load; **`run_target`** does the same for a `module` or `module:callable` string |
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
**User-facing recipe:** **`docs/guide/packaging.md`**.
**Archived design:** [archive/koskari-scripts-packaging.md](archive/koskari-scripts-packaging.md).

### Continuations (summary)

- **Delimited**: `reset`, `shift`, and `reset-at` / `shift-at`, which take
  an evaluated tag matched by identity (`None` is the default tag). A shift
  captures its region and aborts to its prompt before the handler runs, so
  the handler stands where the region stood. Each call of the continuation,
  through the **CALL** family, installs a copy of the region on a DELIM at the
  call site. Extents inside the region are left at the shift and entered
  again per call. A delimited continuation cannot be called from Python
  ([EVALUATOR.md](EVALUATOR.md) §6.4).
- **Transfers at a continuation boundary**: a `break` or `continue` in a
  shift handler reaches only a loop inside the handler. A `return` in a
  handler written in a called function, and a `return` or `yield` in a
  installed copy's own frame, raise `KoskariPromptUnreachable`, which is a
  `RuntimeError` ([EVALUATOR.md](EVALUATOR.md) §6.5).
- **Undelimited**: `call/cc`, `let/cc`, `capture/cc`; Python invoke via **`KoskariContinuationApply`**.
- **Dynamic wind**: `dyn-wind`; **PUSH_WIND** / **POP_WIND** on transfer.
- **`koskari.solve`**: `amb`, `fail`, `require`, `solve`, `solve-first`,
  delimited by a gensym tag of the module's own, so a `reset` in the thunk
  being solved is not a search delimiter.
- **Transfer goals (done):** a continuation invoke, a shift's abort and a
  handler's completion each push a goal recording where control is going;
  the prologue re-derives each step from the live chain, waits while a
  thunk or `__exit__` runs, nests when a thunk invokes another
  continuation, and frees every goal on a failed step before the raise
  walks the chain as it stands. See [EVALUATOR.md §6](EVALUATOR.md); tests
  in **`TestDynWindContinuations`** and **`TestDelimitedTransferNested`**.
- **Frame depth limit (done):** frame depth is bounded by the process-wide
  frame depth limit (default 10000, set through
  `koskari.lib.set_frame_depth_limit`), not by `sys.getrecursionlimit()`;
  an install, a continuation call, or a generator resume that would exceed
  it raises `RecursionError` into the calling frame. See
  [EVALUATOR.md §7.7](EVALUATOR.md).

---

## Implementation checklist (native VM)

Items below are **done** in Koskari unless noted in [TODO.md](TODO.md).

**Types and refs** — atoms, pairs, namespaces, all ref kinds, formals, values, defer, stack, domain.

**Operators (C)** — reducing arithmetic/bitwise, chained compares, membership.

**Utilities (C)** — `apply`, `reapply`, `identity`, `constantly`, collection builders, `build_str`.

**Evaluator core** -- `KosContext`, dynamic chain (`KosDynNode`), transfer goals
(waiting across thunks and awaits, nested on a thunk's invoke, freed on a
failed step before **`try_local`**), the extent contract and obligations, `koskari_eval`, coroutine/async-gen
wrappers, GC hooks.

**Bytecode** — `Code`, full opcode surface per [OPCODES.md](OPCODES.md) §1.6 (runtime implementations for all shipped opcodes except compile-time labels and **THEEND**).

**Continuation opcodes** — PUSH/POP PROMPT/SHIFT/CC, CAPTURE_CC, PUSH/POP WIND; continuations invoke through CALL.

**Namespace opcodes** — PUSH_NS / POP_NS; **VAR_STORE**.

---

## Abandoned or deferred design choices

- CPython bytecode as a Koskari compilation target
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
| GIL cooperation | [EVALUATOR.md](EVALUATOR.md) §1.6; replaced step-counter design: [archive/gil-cooperation.md](archive/gil-cooperation.md) |
