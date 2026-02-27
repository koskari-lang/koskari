# Feature Catalog Summary

This document provides a high-level summary of all features identified from both implementations.

## Language Features

### Special Forms
- **Core**: quote, quasiquote, begin, lambda, function, let, define, setq, delq, refq (quote, setq, delq, refq, bindq, define implemented as compile/quote, compile/setq, compile/delq, compile/refq, compile/bindq, compile/define)
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
- **Logical**: and, or, not (compile/not implemented via OBJECT_NOT; and/or not yet implemented as specials)
- **Object introspection**: repr, truth, iter, str, len, type, dir, vars, aiter, divmod (implemented as compile/repr, compile/truth, compile/iter, compile/str, compile/len, compile/type, compile/dir, compile/vars, compile/aiter, compile/divmod via OBJECT_* opcodes)
- **Collections**: build-tuple, build-list, build-set, build-dict, build-str
- **Items**: item, set-item, del-item, ref-item (implemented as compile/item, compile/set-item, compile/del-item, compile/ref-item)

### Macros
- **Reader Macros**: Character-based parse-time transformation
- **Compile-time Macros**: defmacro, defalias
- **Expansion**: macroexpand-1

### Control Flow
- **Conditionals**: cond
- **Loops**: while, for-each, await-for-each
- **Exceptions**: try/except/else/finally, raise
- **Context**: with, await-with

### Functions
- **Definition**: function, lambda (named and anonymous)
- **Calling**: Direct call, method call, recur
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
- **Opcodes**: Comprehensive opcode set for all operations; see "Opcodes: implementation status" below for which are implemented in the VM.
- **Structure**: Code (with NamespaceSpec), position table, constant pool, extent table
- **Position**: Delta-encoded line/column tracking
- **Extent table**: Derived from PUSH/POP pairs; O(1) block membership and handler lookup; JUMP locality validation at construction
- **Security**: Read-only memory protection after initialization

### Opcodes: implementation status
The VM in `eval.c` uses a jump table; every opcode has a handler. Handlers are either **fully implemented** or **stubs** (delegate to `op_unimplemented_stub`, which raises at runtime). Status is as of the current koskari implementation.

**Fully implemented** (handler does real work; no stub):
- **Pseudo / stack**: NOOP, LABEL, POSITION (no-op at runtime: advance PC only; no effect), DUP, POP, CONST
- **Pair construction and access**: CAR, CDR, CONS (CAR/CDR pop one value and push car/cdr; CONS arg = count, stack bottom-to-top = first element … tail; TailPtr + ConsFromPtr + Drop + push result)
- **Argument production**: BUILD_ARGS, BUILD_KWDS, BUILD_KWDS_NAMES, COMBINE_ARGS, COMBINE_KWDS (build args/kwds for CALL_NAMES/CALL_STAR), BUILD_FUNCTION (build Function from FunctionSpec; stack [fspec, ...kw_defaults, names_tuple] → [function])
- **Control**: RETURN, AWAIT, YIELD (suspend with value; resumption via send; async gen support), YIELD_FROM (delegate to sub-iterator/gen; KOS_DYN_YIELD_FROM; send/throw/close; handler in koskari/lib/opcode/yield_from.c)
- **Calls**: CALL (arg = N; stack [callable, arg_0, ..., arg_{N-1}]; Continuation/DelimitedContinuation → call_call_continuation; KosExpression/KosFunction → new frame in same loop; other callables → PyObject_Call then push_or_await; **defer** return values are not auto-awaited — pushed as plain values), CALL_NAMES (arg = N; stack [callable, pos_0..pos_{N-1}, kwdv_0..kwdv_{M-1}, names] with names tuple at TOS; kos_callable_call), CALL_STAR (arg = bitflags 1=*args 2=**kwargs; stack [callable, pos, kwd]; kos_callable_call), TAIL_CALL (arg = N; same stack layout as CALL; replaces current frame with callee frame for proper tail-call semantics; compiler emits when call is in tail position), TAIL_CALL_NAMES (arg = N; same stack as CALL_NAMES; kos_callable_tail_call), TAIL_CALL_STAR (arg = bitflags; same stack as CALL_STAR; kos_callable_tail_call)
- **Introspection**: GLOBALS, LOCALS
- **Module import**: IMPORT (absolute dotted import; arg = cpool str; pushes leaf module via `_import_fromlist` sentinel), IMPORT_REL (relative import; arg = cpool `(level:int, name:str|None)` tuple; uses outermost frame globals as anchor), IMPORT_FROM (**PEEK-NOT-POP**: arg = cpool str; reads named attr from TOS module without consuming TOS; every use site must be followed by `POP 1` — structurally guaranteed by `helper_import_from_sequence()`)
- **Variables by name**: VAR (load), VAR_BIND (bind), VAR_CLEAR (delete), VAR_REF (closure ref), VAR_STORE (store TOS to name)
- **Variables by index (fast locals)**: FAST, FAST_BIND, FAST_CLEAR, FAST_REF, FAST_STORE
- **Jumps**: JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR
- **Check jumps**: CHECK_EQ, CHECK_NE, CHECK_LT, CHECK_LE, CHECK_GT, CHECK_GE, CHECK_IN, CHECK_NOT_IN, CHECK_IS, CHECK_IS_NOT, CHECK_IS_INSTANCE, CHECK_IS_SUBCLASS
- **Attributes / items**: ATTR, ATTR_CLEAR, ATTR_REF, ATTR_STORE; ITEM, ITEM_CLEAR, ITEM_REF, ITEM_STORE; CONST_ITEM, CONST_ITEM_CLEAR, CONST_ITEM_REF, CONST_ITEM_STORE
- **Namespace scope**: PUSH_NS, POP_NS (creates child namespace from NamespaceSpec; supports value binding; uses KosDynNs on dynamic chain)
- **Exceptions**: EXCEPTION, EXCEPTION_CLEAR, EXCEPTION_MATCH, RAISE, CHECK_EXCEPTION
- **Try/finally**: PUSH_TRY, PUSH_FINALLY, POP_TRY, POP_FINALLY, CLEAR_TRY, CLEAR_FINALLY (each PUSH_TRY and PUSH_FINALLY pushes its own KosStackBlock; exception dispatch clears the block before jumping to the handler; POP_TRY and POP_FINALLY drop the block, with PopBlock(count) to preserve handler/finally results; region unwind drops the block when BREAK/CONTINUE passes through)
- **Delimited continuations**: PUSH_PROMPT (pushes COW stack block + KOS_DYN_PROMPT), PUSH_SHIFT (captures continuation, unwinds to prompt, enters shift block; SHIFT is a simple marker), POP_SHIFT (pops result; TRUNCATE_AND_INJECT unwinds through SHIFT+PROMPT and injects value onto outer stack), POP_PROMPT (normal: drops stack block, pops prompt, pushes result; continuation-return: unwinds to invocation site via DELIMITED_RETURN_FRAME)
- **Undelimited continuations**: PUSH_CC, CAPTURE_CC, POP_CC (capture); invocation via CALL 0 or CALL 1 with stack [callable, arg_0, ...]. Continuation carries token_ref (KosEvaluatorToken); same-token invoke applies directly, different-token raises KoskariContinuationApply (propagated to caller).
- **Continuation invocation**: INVOKE_K (delimited: stack-based), INVOKE_K_VAR (delimited: name-based); undelimited via CALL as above
- **Context managers**: PUSH_WITH, POP_WITH (sync and async single-shot `with`; KOS_DYN_WITH; `__aexit__` truthy return swallows exception). PUSH_DYN_WITH, POP_DYN_WITH (multi-shot `dyn-with`; async enter/exit; cloneable; KOS_DYN_DYN_WITH). Both use KOS_DYN_CAP_CATCH for exception swallow. See design/interpreter/with-dyn-with.md.
- **Object protocol**: All OBJECT_* opcodes (comparisons, arithmetic, predicates, len/type/iter/str/repr/vars/dir, bitwise, in/not in, is/is not, isinstance/issubclass, etc.)
- **Iteration**: ITER_NEXT (region-only, must be inside PUSH_FOREACH; uses region->iter; arg = else label; auto-await), NEXT (stack-based; arg = 1 or 2; 1-arg: StopIteration raises; 2-arg: StopIteration pushes default; auto-await for sync iter, Python async iter, Koskari async gen)
- **Loop control**: PUSH_LOOP, PUSH_FOREACH, POP_LOOP, CLEAR_LOOP, BREAK, CONTINUE (use KOS_DYN_LOOP/KOS_DYN_FOREACH on dynamic chain; each pushes a stack block; PUSH_TRY and PUSH_FINALLY also push their own stack blocks; BREAK/CONTINUE unwind via kos_find_node_for_loop; unwind drops TRY/FINALLY blocks when control passes through; ITER_NEXT at loop head for foreach)

**No runtime implementation** (hardcoded error; not a stub to be implemented later):
- **THEEND**: End-of-code sentinel; if execution reaches it, raises RuntimeError with a fixed message ("THEEND has no runtime implementation"). Never has a runtime implementation.

- **Dynamic wind (partial)**: END_WIND is implemented (terminates before/after hook block, calls `kos_end_hook`; if unwind was in progress, resumes unwinding and delivers any stashed return). PUSH_WIND and POP_WIND remain stubs. Unified extent wind/unwind helpers (`kos_dyn_wind_wind`, `kos_dyn_wind_unwind`, etc.) are used by both simple pop and transfer plans; see design/interpreter/extent-wind-unwind.md.

**Not implemented (stubs)** — hitting these at runtime raises "unimplemented opcode":
- **Calls**: CALL_ATTR, CALL_ATTR_NAMES, CALL_ATTR_STAR; RECUR, RECUR_NAMES, RECUR_STAR; TAIL_RECUR, TAIL_RECUR_NAMES, TAIL_RECUR_STAR
- **Block control (remaining)**: PUSH_CLASS (stub)
- **Wind**: PUSH_WIND, POP_WIND (stubs)

There are no **partially** implemented opcodes; each handler is either full, a stub, or a dedicated "no runtime implementation" handler (THEEND). The authoritative list is the set of handlers in `koskari/lib/eval.c` that do not call `op_unimplemented_stub`.

### Reader (Koskari Layout Syntax)
- **Default syntax**: Indentation-based block syntax with colon-terminated headers; lowers into canonical s-expressions. See `design/language/layout-syntax.md` and `design/language/layout-syntax-formal.md`.
- **Block parsing**: Header terminator is the final `:` at delimiter depth 0; `x:` and `x :` parse identically. Keywords like `as:`, `x:` remain intact inside headers.
- **Probe pass + break marker**: Reader uses a two-step process to locate header end without lexer ambiguity (RecordingStream, MARK event).
- **Layout heads**: Block heads resolve via `layout/<head>` lookup; handlers receive `(header . body)` and optional peer blocks. Implemented: layout/if (peers: elif, else), layout/try (peers: except, else, finally). Fallback: `layout/block` for unknown heads.
- **Mixed mode**: Pure s-expr syntax embeddable anywhere; layout and s-expr alternate by context.

### Compiler
- **Phases**: Parse → Expand → Compile → Optimize → Generate (full pipeline in progress)
- **Components**: Compiler, CodeContext, ByteCodeFragment (implemented in `koskari.compile`)
- **Optimization**: Constant folding, dead code elimination, fast paths (planned)
- **Implemented specials**: compile/bytecode, compile/begin, compile/call, compile/define, compile/lambda, compile/let, compile/parameterize, compile/cond, compile/if, compile/when, compile/unless, compile/while, compile/for-each, compile/break, compile/continue, compile/try, compile/raise, compile/check-exception, compile/clear-exception, compile/load-exception, compile/setq, compile/refq, compile/bindq, compile/delq, compile/attr, compile/del-attr, compile/set-attr, compile/ref-attr, compile/item, compile/ref-item, compile/del-item, compile/set-item, compile/quote, compile/car, compile/cdr, compile/cons, **import**, **import-from**, **compile/= compile/!= compile/< compile/<= compile/> compile/>= compile/in compile/not-in compile/is compile/is-not compile/isinstance compile/issubclass compile/+ compile/- compile/* compile/** compile/& compile/| compile/^ compile/// compile// compile/@ compile/% compile/<< compile/>> compile/divmod compile/not compile/repr compile/truth compile/iter compile/~ compile/str compile/len compile/type compile/dir compile/vars compile/aiter** (see design/SPECIALS.md)

### Evaluator
- **Model**: Custom drive loop (`koskari_eval` in eval.c). Callable types Expression and Function (func.c) return a KosCoroutine; driving the coroutine (await/send) runs the evaluator. Continuation invocation from Python raises `KoskariContinuationApply` for upwards transfer (does not return a coroutine).
- **Frame**: Custom frame type with stack and namespace
- **Blocks**: try, loop, with, finally management

### Creating and calling functions and expressions
- **Code**: Compiled bytecode is stored in a `Code` object (namespace spec, instructions, constant pool, position table). The compiler (`koskari.compile`) is partially implemented: it supports `compile/bytecode`, `compile/begin`, `compile/call`, `compile/define`, `compile/lambda`, `compile/let`, `compile/parameterize`, `compile/cond`, `compile/if`, `compile/when`, `compile/unless`, `compile/while`, `compile/for-each`, `compile/break`, `compile/continue`, `compile/try`, `compile/raise`, `compile/check-exception`, `compile/clear-exception`, `compile/load-exception`, `compile/setq`, `compile/refq`, `compile/bindq`, `compile/delq`, `compile/attr`, `compile/del-attr`, `compile/set-attr`, `compile/ref-attr`, `compile/item`, `compile/ref-item`, `compile/del-item`, `compile/set-item`, `compile/quote`, `compile/car`, `compile/cdr`, `compile/cons`, `import`, `import-from`, and all `OBJECT_*` operator specials (`compile/+`, `compile/-`, `compile/*`, `compile/**`, `compile/&`, `compile/|`, `compile/^`, `compile///`, `compile//`, `compile/@`, `compile/%`, `compile/<<`, `compile/>>`, `compile/divmod`, `compile/=`, `compile/!=`, `compile/<`, `compile/<=`, `compile/>`, `compile/>=`, `compile/in`, `compile/not-in`, `compile/is`, `compile/is-not`, `compile/isinstance`, `compile/issubclass`, `compile/not`, `compile/repr`, `compile/truth`, `compile/iter`, `compile/~`, `compile/str`, `compile/len`, `compile/type`, `compile/dir`, `compile/vars`, `compile/aiter`) (see design/SPECIALS.md). Tests and tools also build Code manually for VM validation.
- **Expressions**: `Code.expression(parent_ns)` builds an **Expression** from a `Code` and a parent **Namespace**. An Expression is nullary: calling it with `()` creates a **Coroutine** that runs that code in that namespace. No argument binding; used for thunks, modules, or single-shot code.
- **Functions**: `Code.function(formals, parent_ns)` builds a **Function** from a `Code`, a **Formals** instance (parameter spec + defaults), and a parent namespace. Calling a Function with `(*args, **kwargs)` binds the formals to the arguments, builds a frame namespace (via the formals’ NamespaceSpec), applies the bindings, then creates a **Coroutine** that runs the code in that frame namespace.
- **Call flow**: Calling an Expression or Function returns a **Coroutine**. Driving the coroutine (Python `await`, or `send(None)`/`send(value)`) invokes `koskari_eval()` until a boundary event (RETURN, RAISE, YIELD, AWAIT). For AWAIT, the VM suspends and the driver steps the Python awaitable; on completion the result is injected and evaluation resumes. Koskari-to-Koskari calls (CALL opcode) replace the current frame with the callee and continue in the same drive loop.

### Runtime
- **Modules**: Module creation and loading via `koskari.importer.module` (`new_module`, `init_module`, `load_module_async`). Each module gets a `ModuleNamespace` backed by a builtins layer assembled by `koskari.importer.builtins.make_builtins()`.
- **Import**: Import system integration via `koskari.importer`. A custom `KoskariPathFinder` is inserted at the front of `sys.meta_path` so that Python's `import` machinery can discover `.ki` files. `KoskariSourceFileLoader.exec_module` calls `load_module_async` through `koskari._sync.run_sync` to bridge the async VM into Python's synchronous import protocol. Install/uninstall is handled by `koskari.importer.install()` / `koskari.importer.uninstall()`; the hook is auto-installed at `koskari` package import time (respects `KOSKARI_NOIMPORTER` environment variable). Koskari code uses the `import` and `import-from` compiler specials, which emit the native `IMPORT`, `IMPORT_REL`, and `IMPORT_FROM` opcodes backed by `PyImport_ImportModuleLevelObject`.
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
- **Quasiquote**: Full implementation review

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
- Drive loop: `koskari_eval` (opcode fetch and dispatch implemented; many opcodes still stubs)
- GC traversal: helpers for all VM structures

**Continuation Support**:
- Delimited multi-shot continuations (reset/shift) and undelimited call/cc (PUSH_CC, CAPTURE_CC, POP_CC) implemented in VM; invocation via CALL; KoskariContinuationApply propagated when invoking undelimited continuation from a different evaluator token
- Dynamic wind/unwind hooks (before/after) used by continuation transfer plans; PUSH_WIND and POP_WIND are stubs; END_WIND is implemented
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

### Ready to Migrate
- Most special forms from python-sibilant (beyond what is already implemented)
- Macro system (reader macros, defmacro, defalias)
- Remaining control flow constructs (class blocks)
- Compiler: remaining specials (and/or logical operators, quasiquote, yield, await, define-values, setq-values), macro expansion, optimization passes
- Remaining opcode handlers: CALL_ATTR*, RECUR*, TAIL_RECUR*, PUSH_CLASS, PUSH_WIND, POP_WIND (BUILD_ARGS, BUILD_KWDS, BUILD_KWDS_NAMES, COMBINE_ARGS, COMBINE_KWDS, PUSH_WITH, POP_WITH, PUSH_DYN_WITH, POP_DYN_WITH, CALL_NAMES, CALL_STAR, TAIL_CALL_NAMES, TAIL_CALL_STAR, END_WIND implemented; see "Opcodes: implementation status" above; with/dyn-with design in design/interpreter/with-dyn-with.md)

### Needs Reconsideration
- CPython bytecode target (abandoned)
- Wide bytecode format
- Module representation

### Namespace and Continuation Binding
- **PUSH_NS / POP_NS**: Create and pop namespace scopes. PUSH_NS arg = count of values to bind; opa=0 uses stack [NSPEC]; opa>0 uses [NSPEC, val0, ..., val(N-1), NAMES]. Uses KosDynNs node; SHIFT unwind restores namespaces correctly.
- **VAR_STORE**: Pop TOS and store in frame namespace under name (supports KosNamespace and dict).
- **INVOKE_K_VAR**: Pop value, look up name in frame namespace, invoke that continuation. Enables shift handlers to bind continuation by name (PUSH_NS + VAR_STORE + INVOKE_K_VAR).
- **CAPTURE_CC**: Implemented. Captures undelimited continuation up to the current PUSH_CC block; continuation holds token_ref; invoke via CALL 0 or CALL 1.

### To Design
- JIT integration
- Additional language features
- Performance optimizations
- Stack block cloning for full continuation semantics
- Lazy frame COW optimization
