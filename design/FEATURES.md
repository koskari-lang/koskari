# Feature Catalog Summary

This document provides a high-level summary of all features identified from both implementations.

## Language Features

### Special Forms
- **Core**: quote, quasiquote, begin, lambda, function, let, define, setq, delq, refq, class (quote, setq, delq, refq, bindq, define implemented as compile/quote, compile/setq, compile/delq, compile/refq, compile/bindq, compile/define; compile/class; **quasiquote, unquote, and unquote-splicing implemented** as compile/quasiquote, compile/unquote, compile/unquote-splicing)
- **Control Flow**: cond, while, for-each, try/except/else/finally, raise, return, break, continue (compile/try, compile/raise, compile/check-exception, compile/clear-exception, compile/load-exception)
- **Context**: with, await-with
- **Generators**: yield, yield-from
- **Async**: await, declare-async, await-for-each
- **Attributes**: attr, set-attr, del-attr, method-call (attr, set-attr, del-attr, ref-attr implemented as compile/attr, compile/set-attr, compile/del-attr, compile/ref-attr)
- **Values**: define-values, setq-values
- **Import**: import, import-from (implemented; see `koskari.importer` and compiler specials `import`, `import-from`)
- **Documentation**: doc

### Operators
- **Arithmetic**: +, -, *, /, //, %, **, @ (implemented as compile/+, compile/-, compile/*, compile//, compile///, compile/%, compile/**, compile/@ via OBJECT_* opcodes with left-to-right constant folding for leading constant arguments)
- **Bitwise**: &, |, ^, ~, <<, >> (implemented as compile/&, compile/|, compile/^, compile/~, compile/<<, compile/>> via OBJECT_* opcodes; &, |, ^ are variadic with constant folding)
- **Comparison**: =, !=, <, <=, >, >=, is, is-not, in, not-in (implemented as compile/=, compile/!=, compile/<, compile/<=, compile/>, compile/>=, compile/is, compile/is-not, compile/in, compile/not-in via OBJECT_* opcodes)
- **Type checks**: isinstance, issubclass (implemented as compile/isinstance, compile/issubclass via OBJECT_IS_INSTANCE, OBJECT_IS_SUBCLASS; variadic type arguments)
- **Logical**: and, or, not (compile/not via OBJECT_NOT; compile/and and compile/or implemented as short-circuit conjunctions/disjunctions via JUMP_AND/JUMP_OR)
- **Object introspection**: repr, truth, iter, str, len, type, dir, vars, aiter, divmod (implemented as compile/repr, compile/truth, compile/iter, compile/str, compile/len, compile/type, compile/dir, compile/vars, compile/aiter, compile/divmod via OBJECT_* opcodes)
- **Collections**: build-tuple, build-list, build-set, build-dict, build-str
- **Items**: item, set-item, del-item, ref-item (implemented as compile/item, compile/set-item, compile/del-item, compile/ref-item)

### Macros
- **Reader macros**: Character-based parse-time transformation (`koskari.reader.parser`; `set_macro_character`, default quote and quasiquote readers).
- **Compile-time macros**: **`define-syntax`** with **`syntax-rules`**, **`macro-rules`**, **`syntax-match`**, or **`macro-match`**; lowered by the corresponding **`compile/syntax-*`** / **`compile/macro-*`** specials (see [SPECIALS.md](SPECIALS.md)). Historical **`defmacro`** / **`defalias`** / **`macroexpand-1`** notes live in [archive/compiler-python-sibilant-historical.md](archive/compiler-python-sibilant-historical.md).
- **RULE vs MATCH**: **RULE** patterns (Scheme-style list patterns) are used by
  **`syntax-rules`** and **`macro-rules`**. **MATCH** patterns (explicit matcher
  rows **`(PATTERN …)`** per rule) target **`syntax-match`** and
  **`macro-match`**; they lower via **`parse_explicit_rule_pattern`** to the same
  matcher IR as the **`MATCH(...)`** combinator in `koskari.matcher`. The
  **macro spec head** selects which language applies; see
  [language/unified-matcher-transformer-model.md](language/unified-matcher-transformer-model.md).
- **Hygiene and syntax boxes**: **`koskari.syntax.Syntax`**, **`koskari.hygiene`**, **`unbox_macro_with_hygiene`** — [language/macro-hygiene-syntax-boxes.md](language/macro-hygiene-syntax-boxes.md); matcher metadata for specials — [api/matcher-hygiene-extensions.md](api/matcher-hygiene-extensions.md)

### Control Flow
- **Conditionals**: cond
- **Loops**: while, for-each, await-for-each
- **Exceptions**: try/except/else/finally, raise
- **Context**: with, await-with

### Functions
- **Definition**: function, lambda (named and anonymous)
- **Calling**: Direct call, method call
- **Formals**: Positional, defaults, keyword-only, *args, **kwargs

### Async/Coroutines
- **Definition**: declare-async, coroutine, coro
- **Operations**: await, yield-from
- **Iteration**: await-for-each
- **Context**: await-with

### Generators
- **Creation**: Automatic via yield
- **Operations**: yield, yield-from
- **Protocol**: Full generator protocol support

## API Features

### Types
- **Symbol**: Immutable identifier
- **Keyword**: Special marker type
- **Pair**: Cons cell (proper/improper)
- **Nil**: Empty list
- **Gensym**: Unique symbol generation
- **Namespace**: Lexical scope
- **Ref**: Mutable reference cell
- **Function**: Compiled function
- **Module**: Module object

### Builtins
- **Pair Ops**: cons, car, cdr, setcar, setcdr, build_unpack_pair
- **Collections**: build_tuple, build_list, build_set, build_dict
- **Values**: values
- **Utilities**: reapply, apply, identity, constantly, _pass
- **Operators**: +, -, *, /, //, @, &, |, ^, in, not-in, build-str

## Interpreter Features

### Bytecode
- **Format**: Custom compact bytecode (4 bytes per instruction)
- **Opcodes**: Comprehensive opcode set for all operations. **Implementation
  status** is documented in [OPCODES.md](OPCODES.md) §1.6: every opcode in
  shipped bytecode has a runtime implementation except **THEEND** (must not
  execute) and compile-time **LABEL** / **POSITION** (normally stripped before
  execution). That section is the authoritative reference.
- **Structure**: Code (with NamespaceSpec), position table, constant pool,
  extent table
- **Position**: Delta-encoded line/column tracking
- **Extent table**: Derived from PUSH/POP pairs; O(1) block membership and
  handler lookup; JUMP locality validation at construction
- **Security**: Read-only memory protection after initialization

### Reader (Koskari Layout Syntax)
- **Default syntax**: Indentation-based block syntax with colon-terminated headers; lowers into canonical s-expressions. See `design/language/layout-syntax.md` and `design/language/layout-syntax-formal.md`.
- **Block parsing**: Header terminator is the final `:` at delimiter depth 0; `x:` and `x :` parse identically. Keywords like `as:`, `x:` remain intact inside headers.
- **Block header parsing**: Single forward pass; `:` is a terminating event at depth 0. No probe or replay; the first bare `:` at delimiter depth 0 ends the header.
- **Layout heads**: Block heads resolve via `layout/<head>` lookup; handlers receive `(header . body)` and optional peer blocks. Implemented: layout/if (peers: elif, else), layout/try (peers: except, else, finally), layout/dyn-wind (peers: before, body, after), layout/define-syntax, layout/syntax-rules, layout/macro-rules, layout/syntax-match, layout/macro-match. Fallback: `layout/block` for unknown heads.
- **Mixed mode**: Pure s-expr syntax embeddable anywhere; layout and s-expr alternate by context.

### Compiler
- **Phases**: Parse → Expand → Compile → Optimize → Generate (full pipeline in progress)
- **Components**: Compiler, CodeContext, ByteCodeFragment (implemented in `koskari.compile`)
- **Optimization**: Reducible constant folding (`koskari/compile/specials/object.py` and related), instruction peephole rewrites and jump folding (`Compiler.compress`), VAR→FAST lowering (`Compiler.convert_fast` in `Compiler.complete`)
- **Implemented specials**: compile/bytecode, compile/begin, compile/call, compile/class, compile/define, **compile/define-syntax**, **compile/syntax-rules**, **compile/macro-rules**, **compile/syntax-match**, **compile/macro-match**, compile/lambda, compile/let, compile/parameterize, compile/cond, compile/if, compile/when, compile/unless, compile/and, compile/or, compile/while, compile/for-each, compile/break, compile/continue, compile/try, compile/raise, compile/check-exception, compile/clear-exception, compile/load-exception, compile/setq, compile/setq-values, compile/define-values, compile/refq, compile/bindq, compile/delq, compile/attr, compile/del-attr, compile/set-attr, compile/ref-attr, compile/item, compile/ref-item, compile/del-item, compile/set-item, compile/quote, **compile/quasiquote, compile/unquote, compile/unquote-splicing**, compile/car, compile/cdr, compile/cons, compile/with, compile/dyn-with, compile/dyn-wind, **import**, **import-from**, **compile/= compile/!= compile/< compile/<= compile/> compile/>= compile/in compile/not-in compile/is compile/is-not compile/isinstance compile/issubclass compile/+ compile/- compile/* compile/** compile/& compile/| compile/^ compile/// compile// compile/@ compile/% compile/<< compile/>> compile/divmod compile/not compile/repr compile/truth compile/iter compile/~ compile/str compile/len compile/type compile/dir compile/vars compile/aiter** (see design/SPECIALS.md)

### Evaluator
- **Model**: Custom drive loop (`koskari_eval` in eval.c). Callable types Expression and Function (func.c) return a KosCoroutine (coro.c); driving the coroutine (await/send) runs the evaluator. Continuation invocation from Python raises `KoskariContinuationApply` for upwards transfer (does not return a coroutine).
- **Frame**: Custom frame type with stack and namespace
- **Blocks**: try, loop, with, finally management

### Creating and calling functions and expressions
- **Code**: Compiled bytecode is stored in a `Code` object (namespace spec, instructions, constant pool, position table). The compiler (`koskari.compile`) is substantially implemented; see the "Implemented specials" list in the Compiler section above and `design/SPECIALS.md` for the full inventory. Tests and tools also build Code manually for VM validation.
- **Expressions**: `Code.expression(parent_ns)` builds an **Expression** from a `Code` and a parent **Namespace**. An Expression is nullary: calling it with `()` creates a **Coroutine** that runs that code in that namespace. No argument binding; used for thunks, modules, or single-shot code.
- **Functions**: `Code.function(formals, parent_ns)` builds a **Function** from a `Code`, a **Formals** instance (parameter spec + defaults), and a parent namespace. Calling a Function with `(*args, **kwargs)` binds the formals to the arguments, builds a frame namespace (via the formals’ NamespaceSpec), applies the bindings, then creates a **Coroutine** that runs the code in that frame namespace.
- **Call flow**: Calling an Expression or Function returns a **Coroutine**. Driving the coroutine (Python `await`, or `send(None)`/`send(value)`) invokes `koskari_eval()` until a boundary event (RETURN, RAISE, YIELD, AWAIT). For AWAIT, the VM suspends and the driver steps the Python awaitable; on completion the result is injected and evaluation resumes. Koskari-to-Koskari calls (CALL opcode) replace the current frame with the callee and continue in the same drive loop.

### Runtime
- **Modules**: Module creation and loading via `koskari.importer.module` (`new_module`, `init_module`, `load_module_async`, and the parallel demand chain `new_module_demand`, `load_module_demand`). Each module gets a `ModuleNamespace` backed by a builtins layer assembled by `koskari.importer.bootstrap.make_builtins()` or `make_builtins_demand()`.
- **Import**: Import system integration via `koskari.importer`. A custom `KoskariPathFinder` is inserted at the front of `sys.meta_path` so that Python's `import` machinery can discover `.ki` files. `KoskariSourceFileLoader.exec_module` uses `exec_module_demand` by default. Install/uninstall is handled by `koskari.importer.install()` / `koskari.importer.uninstall()`; the hook is auto-installed at `koskari` package import time (respects `KOSKARI_NOIMPORTER` environment variable). Koskari code uses the `import` and `import-from` compiler specials, which emit the native `IMPORT`, `IMPORT_REL`, and `IMPORT_FROM` opcodes backed by `PyImport_ImportModuleLevelObject`.
- **Tracebacks**: Line number support

### Continuations
- **Delimited continuations (reset/shift)**: PUSH_PROMPT, PUSH_SHIFT, POP_SHIFT, POP_PROMPT. PUSH_PROMPT owns a COW stack block for the delimited region (SHIFT does not create one). PUSH_SHIFT captures the continuation (from POP_SHIFT+1 to POP_PROMPT) and enters the shift block with k on stack; INVOKE_K and INVOKE_K_VAR invoke captured delimited continuations using the push model (graft frames onto current chain; return to call site when continuation hits POP_PROMPT). Multi-shot; prompt need not be in chain when invoking. Exception propagation past a PROMPT uses `KOS_DYN_CAP_HANDLES_EXCEPTIONS` / `unwind_on_exception` (no kind check in the unwind loop). See design/interpreter/callcc.md.
- **Undelimited call/cc**: PUSH_CC, CAPTURE_CC, POP_CC capture an undelimited continuation (stored with token_ref = owning KosEvaluatorToken). Invocation is via CALL 0 or CALL 1 with stack [callable, arg_0, ...]. If the continuation’s token matches the current evaluator token (or is undelimited with no token_ref), it is applied directly; otherwise the VM sets KoskariContinuationApply and returns -1 so the exception propagates to the caller. Python type KosPyContinuation is callable from Python; invocation always raises `KoskariContinuationApply` to perform upwards transfer via exception (never returns a coroutine). See design/interpreter/evaluator-token-continuation.md.

## Missing/To Design

### Additional Features
- **Domain (ambient dynamic context)**: TLS plumbing, KosDomain type, domain-scoping API, ParamRef, and KOS_DYN_PARAM dyn extent are implemented in `domain.c`/`domain.h`, `ref.c`, and `dynchain.c` (Steps 1–5). KosDomain holds param value store (name -> value); ParamRef resolves via `kos_domain_current()` and `kos_domain_get_param`. KOS_DYN_PARAM nodes support `kos_dyn_push_param`/`kos_dyn_pop_param` with enter/restore and transfer-plan integration. Opcodes PARAM, PUSH_PARAM, POP_PARAM are implemented. The compile/parameterize special form emits PUSH_PARAM/POP_PARAM; consumers read the current parameter via ParamRef(name).deref() (bind-param not yet implemented). See design/interpreter/domain.md and design/interpreter/extent-wind-unwind.md.
- **Comprehensions**: List/dict/set comprehensions
- **Pattern Matching**: match/case style
- **Type System**: Optional type hints/annotations
- **JIT**: Just-in-time compilation
- **let***: Sequential let binding

## Migration Status

### Completed

**Native Types** (C extension with full test coverage):
- Atom types: `symbol`, `keyword`, `gensym()`
- Pair types: `pair`, `nil`, `cons()`, `car()`, `cdr()`, `setcar()`, `setcdr()`, `make_recursive()`, `break_recursive()`, `build_unpack_pair()`; pair methods: `length()`, `follow()`, `unpack()`, `__copy__`, `is_proper()`, `is_recursive()`, `append()`, `extend()`, `splice()`, `clear_position()`, `set_position()`, `fill_position()`, `get_position()` (koskari/lib/pair.c)
- Namespace types: `Namespace`, `NamespaceSpec`, `ModuleNamespace`, `CollectorNamespace`, `Module`
- Reference types: `Ref`, `AttributeRef`, `ItemRef`, `DynamicRef`, `ConstantRef`, `MemoizedRef`, `ParamRef`
- Formals types: `Formals`, `FormalsSpec`
- Values type: `values`
- Defer type: `defer` (single-shot await-handle; not eagerly awaited; implemented in koskari/lib/defer.c)
- Stack types: `Stack`, `BlockStyle` (block-structured operand stack)
- Domain type: `Domain` (koskari.lib.Domain; identity/lifecycle anchor, param value store; TLS in domain.c). ParamRef type: `ParamRef` (koskari.lib.ParamRef; Ref subclass; resolves via active domain)

**Native Operators** (C extension):
- Reducing arithmetic: `+`, `-`, `*`, `/`, `//`, `@`
- Reducing bitwise: `&`, `|`, `^`
- Membership: `in`, `not-in`
- String builder: `build-str`

**Native Utilities** (C extension):
- Function application: `apply`, `reapply`
- Functional primitives: `identity`, `constantly`, `_pass`
- Collection builders: `build_tuple`, `build_list`, `build_set`, `build_dict`

**Evaluator Core** (C extension, eval.c/eval.h, domain.c/domain.h):
- VM structures: `KosFrame`, `KosContext`, `KosHandler`, `KosEvent`
- Domain-scoped TLS: `KosTlsState` (ctx_stack, token_stack), `kos_ctx_current()`, `kos_domain_current()`, `kos_domain_push_scope()`, `kos_domain_pop_scope()` in domain.c
- Dynamic chain: single tagged-union `KosDynNode` (all node kinds); field access via macros only (dynchain.h)
- Allocator caches: dyn-node freelist, KosContinuation freelist (dynchain.c), KosContext cache (eval.c); caps tunable
- Python wrappers: `Continuation`, `Coroutine`, `AsyncGenerator`
- Wind/unwind: `kos_push_wind`, `kos_push_before_wind`, `kos_push_after_wind`, `kos_pop_wind`
- Continuation application: `kos_apply_continuation` with LCA computation and hook execution
- Call operations: internal tail-call for KosExpression/KosFunction (eval.c)
- Drive loop: `koskari_eval` (opcode fetch and dispatch; handlers per [OPCODES.md](OPCODES.md) §1.6)
- GC traversal: helpers for all VM structures

**Continuation Support**:
- Delimited multi-shot continuations (reset/shift) and undelimited call/cc (PUSH_CC, CAPTURE_CC, POP_CC) implemented in VM; invocation via CALL; KoskariContinuationApply propagated when invoking undelimited continuation from a different evaluator token
- Dynamic wind/unwind thunks (before/after) used by continuation transfer plans; PUSH_WIND and POP_WIND run before/after thunks via kos_callable_call; thunk return handling in RETURN opcode resumes unwind or transfer plan
- Flattened async execution
- Eager-step awaitable optimization
- Defer (single-shot await-handle): not eagerly awaited; explicit AWAIT drives it (koskari/lib/defer.c)

**Bytecode System** (C extension, bytecode.c/bytecode.h):
- `Code`: Compiled bytecode storage with read-only protection
  - Compact 4-byte instructions (uint16_t op + uint16_t arg)
  - Delta-encoded position table (only records changes)
  - Memory protection via `mprotect()` after initialization
  - Constant pool with deduplication
  - `KOS_CODE_YIELDS` flag for async generators
- `OpCode`: Enum of all bytecode opcodes (from opcodes.incl)
- `OpStyle`: Enum for opcode argument interpretation
- Full opcode set for: stack ops, control flow, jumps, variables, attributes, items, calls, blocks, object protocol, iteration, exceptions

### Further migration / backlog

Further language and compiler work (slices, variadic comparison chains,
optimization passes, macro hygiene, fenced literals, bundles, and other items)
is tracked in [TODO.md](TODO.md). The live inventory of implemented specials
is [SPECIALS.md](SPECIALS.md).

### Needs Reconsideration
- CPython bytecode target (abandoned)
- Wide bytecode format
- Module representation

### Namespace and Continuation Binding
- **PUSH_NS / POP_NS**: Create and pop namespace scopes. PUSH_NS is EXTRA-style: NSPEC and NAMES (tuple or None) live in the extra operand row (cpool indices); binding count is inferred from `len(names)` or 0 if None. Stack holds only value operands when binding: `[val0, ..., val_{N-1}]` with N = number of names. Uses KosDynNs node; SHIFT unwind restores namespaces correctly.
- **VAR_STORE**: Pop TOS and store in frame namespace under name (supports KosNamespace and dict).
- **INVOKE_K_VAR**: Pop value, look up name in frame namespace, invoke that continuation. Enables shift handlers to bind continuation by name (PUSH_NS + VAR_STORE + INVOKE_K_VAR).
- **CAPTURE_CC**: Implemented. Captures undelimited continuation up to the current PUSH_CC block; continuation holds token_ref; invoke via CALL 0 or CALL 1.

### To Design
- JIT integration
- Additional language features
- Performance optimizations
