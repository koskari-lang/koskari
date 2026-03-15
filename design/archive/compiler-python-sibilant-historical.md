# Compiler Architecture (historical)

This document describes the abandoned CPython-bytecode pipeline (python-sibilant)
and the sibilant-ceval lineage. The current Koskari compiler is in
`koskari/compile/`; see [design/SPECIALS.md](../SPECIALS.md) for implemented
specials.

---

## From python-sibilant (historical; CPython bytecode target abandoned)

Koskari uses custom bytecode and a custom C evaluator, not CPython bytecode. The following describes the original python-sibilant pipeline for reference.

### Compilation Phases

1. **Parsing**: Source → S-expressions
2. **Macro Expansion**: Expand reader and compile-time macros
3. **Compilation**: S-expressions → Pseudops → CPython bytecode
4. **Optimization**: Constant folding, dead code elimination
5. **Code Generation**: Pseudops → CPython bytecode

### Compiler Components

**Compiler**:
- Maintains compilation context
- Tracks variables, namespaces, blocks
- Generates pseudops
- Handles special forms and operators

**Code Context**:
- Variable declarations
- Block stack (try, loop, with, finally)
- Tail-call tracking
- Generator/coroutine detection

**Pseudops**:
- Intermediate representation
- Platform-independent
- Converted to target bytecode

### Special Form Handling

- Lookup in compile-time namespace
- Calls `compile_impl` method
- Returns expansion or bytecode fragment
- Restarts compilation if expansion

### Operator Handling

- Lookup in compile-time namespace
- Calls `compile_impl` method
- Can return constant (compile-time eval)
- Or generates bytecode operations

## From sibilant-ceval

### Compilation Phases

1. **Parsing**: Source → S-expressions
2. **Macro Expansion**: Expand macros
3. **Compilation**: S-expressions → ByteCodeFragment → ByteCode
4. **Optimization**: Instruction compression, fast path conversion
5. **Code Generation**: ByteCode ready for execution

### Compiler Components

**Compiler**:
- Creates ByteCodeFragment instances
- Tracks namespace specifications
- Generates opcodes directly
- Handles special forms and operators

**ByteCodeFragment**:
- Intermediate compilation unit
- Instructions: List of (OpCode, argument)
- Declarations: Variable declarations
- Requests: Variable lookups

**Optimization**:
- Instruction compression (CONST/POP elimination)
- Fast path conversion (VAR → FAST)
- Jump optimization
- Constant propagation

### Namespace Handling

- NamespaceSpec: Specification of namespace
- Local and closure variables tracked
- Fast indexing for local variables
- O(1) lookups via opcode indices

## Migration Notes

### Migrated (Partial)

- **Compiler** and **ByteCodeFragment** (`koskari.compile`): Compiler compiles S-expression forms to bytecode fragments. Special form dispatch via `compile_impl`; head-symbol lookup in module namespace.
- **Implemented specials**: See [design/SPECIALS.md](../SPECIALS.md) for the authoritative list of all currently implemented special forms.
- **Namespace handling**: NamespaceSpec, variable declarations, constant-pool and label resolution in fragment `complete` pass.
- **Parser** (Reader, SourceStream) produces S-expressions; used as input to compiler.

### To Migrate

- Macro expansion (reader macros; `defmacro`, `defalias` — syntax-rules via compile/define-syntax is implemented)
- Optimization passes (constant folding, dead code elimination, VAR→FAST conversion)

### To Reconsider

- Pseudops vs. direct opcode generation
- Optimization strategy
- Compilation caching
- Incremental compilation

### To Design

- Multi-pass optimization
- Type inference (if desired)
- Compile-time evaluation
- Macro compilation
- Integration with JIT (if desired)
