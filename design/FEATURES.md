# Feature Catalog Summary

This document provides a high-level summary of all features identified from both implementations.

## Language Features

### Special Forms
- **Core**: quote, quasiquote, begin, lambda, function, let, define, setq, delq, refq (quote, setq, delq, refq, bindq implemented as compile/quote, compile/setq, compile/delq, compile/refq, compile/bindq)
- **Control Flow**: cond, while, for-each, try, return, break, continue
- **Context**: with, await-with
- **Generators**: yield, yield-from
- **Async**: await, declare-async, await-for-each
- **Attributes**: attr, set-attr, del-attr, method-call (attr, set-attr, del-attr, ref-attr implemented as compile/attr, compile/set-attr, compile/del-attr, compile/ref-attr)
- **Values**: define-values, setq-values
- **Import**: import, import-from
- **Documentation**: doc

### Operators
- **Arithmetic**: +, -, *, /, //, %, **, @
- **Bitwise**: &, |, ^, ~, <<, >>
- **Comparison**: ==, !=, <, <=, >, >=, is, is-not, in, not-in
- **Logical**: and, or, not
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
- **Argument production**: BUILD_FUNCTION (build Function from FunctionSpec; stack [fspec, ...kw_defaults, names_tuple] → [function])
- **Control**: RETURN, AWAIT, YIELD (suspend with value; resumption via send; async gen support), YIELD_FROM (delegate to sub-iterator/gen; KOS_DYN_YIELD_FROM; send/throw/close; handler in koskari/lib/opcode/yield_from.c)
- **Calls**: CALL (arg = N; stack [callable, arg_0, ..., arg_{N-1}]; Continuation/DelimitedContinuation → call_call_continuation; KosExpression/KosFunction → new frame in same loop; other callables → PyObject_Call then push_or_await), TAIL_CALL (arg = N; same stack layout as CALL; replaces current frame with callee frame for proper tail-call semantics; compiler emits when call is in tail position)
- **Introspection**: GLOBALS, LOCALS
- **Variables by name**: VAR (load), VAR_BIND (bind), VAR_CLEAR (delete), VAR_REF (closure ref), VAR_STORE (store TOS to name)
- **Variables by index (fast locals)**: FAST, FAST_BIND, FAST_CLEAR, FAST_REF, FAST_STORE
- **Jumps**: JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR
- **Check jumps**: CHECK_EQ, CHECK_NE, CHECK_LT, CHECK_LE, CHECK_GT, CHECK_GE, CHECK_IN, CHECK_NOT_IN, CHECK_IS, CHECK_IS_NOT, CHECK_IS_INSTANCE, CHECK_IS_SUBCLASS
- **Attributes / items**: ATTR, ATTR_CLEAR, ATTR_REF, ATTR_STORE; ITEM, ITEM_CLEAR, ITEM_REF, ITEM_STORE; CONST_ITEM, CONST_ITEM_CLEAR, CONST_ITEM_REF, CONST_ITEM_STORE
- **Namespace scope**: PUSH_NS, POP_NS (creates child namespace from NamespaceSpec; supports value binding; uses KosDynNs on dynamic chain)
- **Exceptions**: EXCEPTION, EXCEPTION_CLEAR, EXCEPTION_MATCH, RAISE, CHECK_EXCEPTION
- **Try/finally**: PUSH_TRY, PUSH_FINALLY, POP_TRY, POP_FINALLY, CLEAR_TRY, CLEAR_FINALLY
- **Delimited continuations**: PUSH_PROMPT, SET_PROMPT_HANDLER, POP_PROMPT, SHIFT
- **Undelimited continuations**: PUSH_CC, CAPTURE_CC, POP_CC (capture); invocation via CALL 0 or CALL 1 with stack [callable, arg_0, ...]. Continuation carries token_ref (KosEvaluatorToken); same-token invoke applies directly, different-token raises KoskariContinuationApply (propagated to caller).
- **Continuation invocation**: INVOKE_K (delimited: stack-based), INVOKE_K_VAR (delimited: name-based); undelimited via CALL as above
- **Object protocol**: All OBJECT_* opcodes (comparisons, arithmetic, predicates, len/type/iter/str/repr/vars/dir, bitwise, in/not in, is/is not, isinstance/issubclass, etc.)
- **Iteration**: ITER_NEXT (region-only, must be inside PUSH_FOREACH; uses region->iter; arg = else label; auto-await), NEXT (stack-based; arg = 1 or 2; 1-arg: StopIteration raises; 2-arg: StopIteration pushes default; auto-await for sync iter, Python async iter, Koskari async gen)
- **Loop control**: PUSH_LOOP, PUSH_FOREACH, POP_LOOP, CLEAR_LOOP, BREAK, CONTINUE (use KosDynRegion LOOP/FOREACH on dynamic chain; PUSH_LOOP pushes stack block; BREAK/CONTINUE unwind via kos_find_region_for_loop; ITER_NEXT at loop head for foreach)

**No runtime implementation** (hardcoded error; not a stub to be implemented later):
- **THEEND**: End-of-code sentinel; if execution reaches it, raises RuntimeError with a fixed message ("THEEND has no runtime implementation"). Never has a runtime implementation.

- **Dynamic wind (partial)**: END_WIND is implemented (terminates before/after hook block, calls `kos_end_hook`; if unwind was in progress, resumes unwinding and delivers any stashed return). PUSH_WIND and POP_WIND remain stubs. Unified extent wind/unwind helpers (`kos_dyn_wind_wind`, `kos_dyn_wind_unwind`, etc.) are used by both simple pop and transfer plans; see design/interpreter/extent-wind-unwind.md.

**Not implemented (stubs)** — hitting these at runtime raises "unimplemented opcode":
- **Argument building**: BUILD_ARGS, BUILD_KWDS, BUILD_KWDS_NAMES, COMBINE_ARGS, COMBINE_KWDS (BUILD_FUNCTION is implemented)
- **Calls**: CALL_NAMES, CALL_STAR; CALL_ATTR, CALL_ATTR_NAMES, CALL_ATTR_STAR; RECUR, RECUR_NAMES, RECUR_STAR; TAIL_CALL_NAMES, TAIL_CALL_STAR; TAIL_RECUR, TAIL_RECUR_NAMES, TAIL_RECUR_STAR
- **Block control**: PUSH_WITH, POP_WITH implemented (single-shot `with`, sync only). PUSH_BEGIN, PUSH_CLASS; POP_BEGIN. Multi-shot `dyn-with` (PUSH_DYN_WITH/POP_DYN_WITH) specified in design/interpreter/with-dyn-with.md; opcodes not yet in opcodes.incl.
- **Wind / region**: PUSH_WIND, POP_WIND (stubs); PUSH_REGION, POP_REGION

There are no **partially** implemented opcodes; each handler is either full, a stub, or a dedicated "no runtime implementation" handler (THEEND). The authoritative list is the set of handlers in `koskari/lib/eval.c` that do not call `op_unimplemented_stub`.

### Compiler
- **Phases**: Parse → Expand → Compile → Optimize → Generate (full pipeline in progress)
- **Components**: Compiler, CodeContext, ByteCodeFragment (implemented in `koskari.compile`)
- **Optimization**: Constant folding, dead code elimination, fast paths (planned)
- **Implemented specials**: compile/bytecode, compile/begin, compile/call, compile/lambda, compile/let, compile/parameterize, compile/setq, compile/refq, compile/bindq, compile/delq, compile/attr, compile/del-attr, compile/set-attr, compile/ref-attr, compile/item, compile/ref-item, compile/del-item, compile/set-item, compile/quote, compile/car, compile/cdr, compile/cons (see design/SPECIALS.md)

### Evaluator
- **Model**: Custom drive loop (`koskari_eval` in eval.c). Callable types Expression and Function (func.c) return a KosCoroutine; driving the coroutine (await/send) runs the evaluator. Continuation call returns a KosCoroutine that resumes the captured segment.
- **Frame**: Custom frame type with stack and namespace
- **Blocks**: try, loop, with, finally management

### Creating and calling functions and expressions
- **Code**: Compiled bytecode is stored in a `Code` object (namespace spec, instructions, constant pool, position table). The compiler (`koskari.compile`) is partially implemented: it supports `compile/bytecode`, `compile/begin`, `compile/call`, `compile/lambda`, `compile/let`, `compile/parameterize`, `compile/setq`, `compile/refq`, `compile/bindq`, `compile/delq`, `compile/attr`, `compile/del-attr`, `compile/set-attr`, `compile/ref-attr`, `compile/item`, `compile/ref-item`, `compile/del-item`, `compile/set-item`, `compile/quote`, `compile/car`, `compile/cdr`, and `compile/cons` (see design/SPECIALS.md). Tests and tools also build Code manually for VM validation.
- **Expressions**: `Code.expression(parent_ns)` builds an **Expression** from a `Code` and a parent **Namespace**. An Expression is nullary: calling it with `()` creates a **Coroutine** that runs that code in that namespace. No argument binding; used for thunks, modules, or single-shot code.
- **Functions**: `Code.function(formals, parent_ns)` builds a **Function** from a `Code`, a **Formals** instance (parameter spec + defaults), and a parent namespace. Calling a Function with `(*args, **kwargs)` binds the formals to the arguments, builds a frame namespace (via the formals’ NamespaceSpec), applies the bindings, then creates a **Coroutine** that runs the code in that frame namespace.
- **Call flow**: Calling an Expression or Function returns a **Coroutine**. Driving the coroutine (Python `await`, or `send(None)`/`send(value)`) invokes `koskari_eval()` until a boundary event (RETURN, RAISE, YIELD, AWAIT). For AWAIT, the VM suspends and the driver steps the Python awaitable; on completion the result is injected and evaluation resumes. Koskari-to-Koskari calls use `kos_koscall` / `kos_tail_koscall` (CALL opcode); the callee is a KosExpression or KosFunction, so the runtime creates a new frame and pushes it onto the dynamic chain and continues the same drive loop.

### Runtime
- **Modules**: Module creation and loading
- **Import**: Import system integration
- **Tracebacks**: Line number support

### Continuations
- **Delimited continuations (reset/shift)**: PUSH_PROMPT, SET_PROMPT_HANDLER, POP_PROMPT, SHIFT capture the continuation up to a prompt; INVOKE_K and INVOKE_K_VAR invoke captured delimited continuations. Multi-shot; transfer plans and LCA-based wind/unwind run on invocation. See design/interpreter/callcc.md.
- **Undelimited call/cc**: PUSH_CC, CAPTURE_CC, POP_CC capture an undelimited continuation (stored with token_ref = owning KosEvaluatorToken). Invocation is via CALL 0 or CALL 1 with stack [callable, arg_0, ...]. If the continuation’s token matches the current evaluator token (or is undelimited with no token_ref), it is applied directly; otherwise the VM sets KoskariContinuationApply and returns -1 so the exception propagates to the caller. Python type KosPyContinuation is callable; cross-token invoke raises KoskariContinuationApply. See design/interpreter/evaluator-token-continuation.md.

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
- Pair types: `pair`, `nil`, `cons()`, `car()`, `cdr()`, `setcar()`, `setcdr()`, `make_recursive()`, `break_recursive()`, `build_unpack_pair()`
- Namespace types: `Namespace`, `NamespaceSpec`, `ModuleNamespace`, `CollectorNamespace`, `Module`
- Reference types: `Ref`, `AttributeRef`, `ItemRef`, `DynamicRef`, `ConstantRef`, `MemoizedRef`, `ParamRef`
- Formals types: `Formals`, `FormalsSpec`
- Values type: `values`
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
- Call operations: `kos_koscall`, `kos_tail_koscall`
- Drive loop: `koskari_eval` (opcode fetch and dispatch implemented; many opcodes still stubs)
- GC traversal: helpers for all VM structures

**Continuation Support**:
- Delimited multi-shot continuations (reset/shift) and undelimited call/cc (PUSH_CC, CAPTURE_CC, POP_CC) implemented in VM; invocation via CALL; KoskariContinuationApply propagated when invoking undelimited continuation from a different evaluator token
- Dynamic wind/unwind hooks (before/after) used by continuation transfer plans; PUSH_WIND/POP_WIND/END_WIND opcodes are stubbed
- Flattened async execution
- Eager-step awaitable optimization

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
- Most special forms from python-sibilant (beyond compile/bytecode, compile/begin, compile/call, lambda)
- Remaining operators (%, **, ~, <<, >>, comparisons)
- Macro system (reader macros, defmacro, defalias)
- Remaining control flow constructs (with, begin, class blocks)
- Compiler: remaining specials, macro expansion, optimization passes
- Remaining opcode handlers: BUILD_ARGS, BUILD_KWDS, COMBINE_*, CALL_NAMES, CALL_STAR, CALL_ATTR*, RECUR*, TAIL_CALL_NAMES, TAIL_CALL_STAR, TAIL_RECUR*, PUSH_DYN_WITH, POP_DYN_WITH, PUSH_BEGIN, PUSH_CLASS, PUSH_WIND, POP_WIND, END_WIND, PUSH_REGION, POP_REGION (PUSH_WITH, POP_WITH implemented; see "Opcodes: implementation status" above; with/dyn-with design in design/interpreter/with-dyn-with.md)

### Needs Reconsideration
- CPython bytecode target (abandoned)
- Wide bytecode format
- Module representation
- Import system strategy

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
