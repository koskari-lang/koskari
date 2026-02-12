# Feature Catalog Summary

This document provides a high-level summary of all features identified from both implementations.

## Language Features

### Special Forms
- **Core**: quote, quasiquote, begin, lambda, function, let, define, setq, delq, refq
- **Control Flow**: cond, while, for-each, try, return, break, continue
- **Context**: with, await-with
- **Generators**: yield, yield-from
- **Async**: await, declare-async, await-for-each
- **Attributes**: attr, set-attr, del-attr, method-call
- **Values**: define-values, setq-values
- **Import**: import, import-from
- **Documentation**: doc

### Operators
- **Arithmetic**: +, -, *, /, //, %, **, @
- **Bitwise**: &, |, ^, ~, <<, >>
- **Comparison**: ==, !=, <, <=, >, >=, is, is-not, in, not-in
- **Logical**: and, or, not
- **Collections**: build-tuple, build-list, build-set, build-dict, build-str
- **Items**: item, set-item, del-item

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
The VM in `eval.c` uses a jump table; every opcode has a handler. Handlers are either **fully implemented**, **partially implemented**, or **stubs** (delegate to `op_unimplemented_stub`, which raises at runtime). Status is as of the current koskari implementation.

**Fully implemented** (handler does real work; no stub):
- **Pseudo / stack**: NOOP, LABEL, POSITION (no-op at runtime: advance PC only; no effect), DUP, POP, CONST
- **Control**: RETURN, AWAIT, YIELD (suspend with value; resumption via send; async gen support)
- **Calls**: CALL (arg = N; stack [callable, arg_0, ..., arg_{N-1}]; Continuation/DelimitedContinuation → call_call_continuation; KosExpression/KosFunction → new frame in same loop; other callables → PyObject_Call then push_or_await)
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

**Not implemented (stubs)** — hitting these at runtime raises "unimplemented opcode":
- **Control / mode**: YIELD_FROM
- **Argument building**: BUILD_ARGS, BUILD_KWDS, BUILD_KWDS_NAMES, COMBINE_ARGS, COMBINE_KWDS
- **Calls**: CALL_NAMES, CALL_STAR; CALL_ATTR, CALL_ATTR_NAMES, CALL_ATTR_STAR; RECUR, RECUR_NAMES, RECUR_STAR; TAIL_CALL, TAIL_CALL_NAMES, TAIL_CALL_STAR; TAIL_RECUR, TAIL_RECUR_NAMES, TAIL_RECUR_STAR
- **Block control**: PUSH_WITH, PUSH_BEGIN, PUSH_CLASS; POP_WITH, POP_BEGIN
- **Wind / region**: PUSH_WIND, POP_WIND, END_WIND; PUSH_REGION, POP_REGION

There are no **partially** implemented opcodes; each handler is either full, a stub, or a dedicated "no runtime implementation" handler (THEEND). The authoritative list is the set of handlers in `koskari/lib/eval.c` that do not call `op_unimplemented_stub`.

### Compiler
- **Phases**: Parse → Expand → Compile → Optimize → Generate
- **Components**: Compiler, CodeContext, ByteCodeFragment
- **Optimization**: Constant folding, dead code elimination, fast paths

### Evaluator
- **Model**: Custom drive loop (`koskari_eval` in eval.c). Callable types Expression and Function (func.c) return a KosCoroutine; driving the coroutine (await/send) runs the evaluator. Continuation call returns a KosCoroutine that resumes the captured segment.
- **Frame**: Custom frame type with stack and namespace
- **Blocks**: try, loop, with, finally management

### Creating and calling functions and expressions
- **Code**: Compiled bytecode is stored in a `Code` object (namespace spec, instructions, constant pool, position table). Code is produced by the compiler (not yet implemented); tests and tools build it manually.
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
- Pair types: `pair`, `nil`, `cons()`, `car()`, `cdr()`, `setcar()`, `setcdr()`, `build_unpack_pair()`
- Namespace types: `Namespace`, `NamespaceSpec`, `ModuleNamespace`, `CollectorNamespace`, `Module`
- Reference types: `Ref`, `AttributeRef`, `ItemRef`, `DynamicRef`, `ConstantRef`, `MemoizedRef`
- Formals types: `Formals`, `FormalsSpec`
- Values type: `values`
- Stack types: `Stack`, `BlockStyle` (block-structured operand stack)

**Native Operators** (C extension):
- Reducing arithmetic: `+`, `-`, `*`, `/`, `//`, `@`
- Reducing bitwise: `&`, `|`, `^`
- Membership: `in`, `not-in`
- String builder: `build-str`

**Native Utilities** (C extension):
- Function application: `apply`, `reapply`
- Functional primitives: `identity`, `constantly`, `_pass`
- Collection builders: `build_tuple`, `build_list`, `build_set`, `build_dict`

**Evaluator Core** (C extension, eval.c/eval.h):
- VM structures: `KosFrame`, `KosContext`, `KosHandler`, `KosEvent`
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
- Most special forms from python-sibilant
- Remaining operators (%, **, ~, <<, >>, comparisons)
- Macro system
- Control flow constructs
- Function system
- Compiler architecture from sibilant-ceval
- Remaining opcode handlers (dispatch loop in place; variables, calls, blocks, etc. still stubbed)

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
