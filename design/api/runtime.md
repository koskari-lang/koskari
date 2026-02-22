# Runtime System

## From python-sibilant

### Module System

**Module Creation**:
- `new_module(name)`: Create new module
- `init_module(module, source_stream, builtins)`: Initialize module
- `load_module(module)`: Load module into sys.modules

**Module Features**:
- Module-level namespace
- `__dict__`-like interface
- Docstring support
- Builtins injection

### Import System

**Import Integration**:
- Extends Python's `importlib`
- Supports `.ki` file extension
- Module discovery via `sys.path`
- Relative imports (`.` prefix)

**Import Functions**:
- `import NAME`: Import module
- `import-from NAME MEMBER...`: Import members

### Compilation

**Compile to File**:
- Compile `.ki` to `.pyc`
- Loaded by default Python importer
- Still requires koskari runtime (not standalone)

**Line Numbers**:
- Line number table in bytecode
- Tracebacks show koskari source lines
- Interleaves Python and koskari in tracebacks

## From sibilant-ceval

### Custom Runtime

**Bytecode Execution**:
- Custom ceval loop
- Frame and stack management
- Namespace handling
- Exception propagation

**Module System**:
- Module backed by ModuleNamespace
- References act like attributes
- O(1) Ref lookups via opcodes

## Current Implementation (eval.c)

### Evaluator Core

The Koskari evaluator provides the runtime execution engine:

**Python-Facing Wrappers**:
- `Coroutine`: Wraps `KosContext` for async function execution
- `AsyncGenerator`: Wraps `KosContext` for async generator execution
- `Continuation`: Captured undelimited continuation; callable from Python raises `KoskariContinuationApply` for upwards transfer (does not return)

**Execution Model**:
- Single evaluator with heap-resident frame stack
- Flattened async: one C call per drive/resume
- Eager-step awaitables to avoid unnecessary suspension

**Protocol Support**:
- Awaitable protocol (`__await__`, `send`, `throw`, `close`)
- Async iterator protocol (`__aiter__`, `__anext__`)
- Callable protocol for continuations

### Dynamic Wind/Unwind

Runtime support for `dynamic-wind` semantics:
- `kos_push_wind(ctx, before, after)`: Install wind handlers
- Handlers invoked on:
  - Normal entry/exit of protected region
  - Continuation application crossing region boundary
  - Exception propagation crossing region boundary

## Migration Notes

### Completed

- Evaluator core structure (KosContext, KosFrame, KosHandler)
- Python wrapper types (Coroutine, AsyncGenerator, Continuation)
- Wind/unwind hook infrastructure
- Continuation capture and application
- GC integration for wrapper types

### To Migrate

- Module creation and loading
- Import system integration
- Line number support
- Traceback integration

### To Reconsider

- `.pyc` compilation (still needed with custom bytecode?)
- Import system (Python's vs. custom)
- Module representation (dict vs. namespace)

### To Design

- Module caching strategy
- Hot reload support
- Module introspection
- Plugin system
- Traceback integration with Koskari frames
