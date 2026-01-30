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

## Migration Notes

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
