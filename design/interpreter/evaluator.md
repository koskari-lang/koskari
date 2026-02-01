# Evaluation Model

## From python-sibilant

### CPython Evaluation

- Uses CPython's evaluation loop
- Koskari bytecode executed by Python interpreter
- Full Python interoperability
- Python tracebacks and debugging

### Trampoline TCO

- Functions marked with `@trampoline`
- Tail calls bounce out of frame
- Evaluated in trampoline loop
- Stack doesn't grow
- Side effect: collapsed call stack

## From sibilant-ceval

### Custom Ceval Loop

**Frame Execution**:
- Custom frame type (KosFrame)
- Custom stack type (KosStack)
- Custom namespace type (Namespace)
- Opcode dispatch loop

**Frame Structure**:
- Bytecode instructions
- Stack for values
- Namespace for variables
- Block stack for control flow
- Defer slot for yield-from/await

**Stack Management**:
- Value stack for evaluation
- Block stack for try/loop/with
- Exception handling
- Cleanup on block exit

**Namespace Management**:
- Local variables
- Closure variables (Ref cells)
- Global variables (parent namespace)
- Fast indexing for locals

### Opcode Execution

**Dispatch**:
- Switch on opcode
- Execute operation
- Update stack/namespace
- Handle control flow

**Optimizations**:
- Fast variable access (index-based)
- Optimized method calls
- Tail call detection
- Exception handling

### Block Management

**Block Types**:
- TRY: Exception handling
- LOOP: While/for-each loops
- WITH: Context managers
- FINALLY: Cleanup blocks
- AWAIT_FOREACH: Async iteration
- AWAIT_WITH: Async context managers

**Block Stack**:
- Tracks nested blocks
- Handles cleanup on exit
- Supports break/continue
- Exception propagation

## Migration Notes

### To Migrate

- Custom ceval loop (from sibilant-ceval)
- Frame and stack management
- Namespace handling
- Block management
- Opcode dispatch

### To Reconsider

- Trampoline TCO vs. direct tail calls
- Stack representation
- Frame representation
- Exception handling strategy

### To Design

- JIT compilation integration
- Profiling and optimization
- Debugging support
- Integration with `call/cc`

## Related Documentation

- [execution-invariants.md](execution-invariants.md) — Koskari execution model
  invariants and design commitments (target semantics)
