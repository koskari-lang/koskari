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
- **TCO**: Trampoline-based or direct tail calls
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
- **Format**: Custom bytecode (sibilant-ceval) or CPython bytecode (python-sibilant)
- **Opcodes**: Comprehensive opcode set for all operations
- **Structure**: NamespaceSpec, FunctionSpec, ByteCode
- **Position**: Line/column tracking

### Compiler
- **Phases**: Parse → Expand → Compile → Optimize → Generate
- **Components**: Compiler, CodeContext, ByteCodeFragment
- **Optimization**: Constant folding, dead code elimination, fast paths

### Evaluator
- **Model**: Custom ceval loop (sibilant-ceval) or CPython (python-sibilant)
- **Frame**: Custom frame type with stack and namespace
- **Blocks**: try, loop, with, finally management
- **TCO**: Trampoline or direct tail calls

### Runtime
- **Modules**: Module creation and loading
- **Import**: Import system integration
- **Tracebacks**: Line number support

## Missing/To Design

### Call/CC
- **Status**: Blocking issue in sibilant-ceval
- **Requirements**: Bounded and unbounded continuations
- **Design**: Context boundaries, continuation capture/invocation

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

**Native Operators** (C extension):
- Reducing arithmetic: `+`, `-`, `*`, `/`, `//`, `@`
- Reducing bitwise: `&`, `|`, `^`
- Membership: `in`, `not-in`
- String builder: `build-str`

**Native Utilities** (C extension):
- Function application: `apply`, `reapply`
- Functional primitives: `identity`, `constantly`, `_pass`
- Collection builders: `build_tuple`, `build_list`, `build_set`, `build_dict`

### Ready to Migrate
- Most special forms from python-sibilant
- Remaining operators (%, **, ~, <<, >>, comparisons)
- Macro system
- Control flow constructs
- Function system
- Bytecode format from sibilant-ceval
- Compiler architecture from sibilant-ceval
- Evaluator from sibilant-ceval

### Needs Reconsideration
- CPython bytecode target (abandoned)
- Trampoline TCO vs. direct tail calls
- Wide bytecode format
- Module representation
- Import system strategy

### To Design
- Call/CC implementation
- JIT integration
- Additional language features
- Performance optimizations
