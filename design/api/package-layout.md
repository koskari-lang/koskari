# Package layout and imports

Koskari is organized by use case. Internal code and tests import only from the **direct definition location** for each symbol. The top-level package does not re-export; any future re-exports would be for external (non-koskari) convenience only.

## Package structure

```
koskari/
├── __init__.py       # Minimal: docstring only, no re-exports
├── exceptions.py     # KoskariException, NotYetImplemented, KoskariSyntaxError
├── types.py          # Value types (from lib) + Python predicates/helpers
├── parse.py          # Reader, SourceStream, reader macros, parse errors
├── dis.py            # Disassembler for Code objects
├── compile/          # Compiler, ByteCodeFragment, special forms
│   ├── __init__.py
│   ├── compiler.py
│   ├── exceptions.py # CompilerException, CompilerMode, UncaughtCompilerException
│   ├── fragment.py   # ByteCodeFragment, unique_append
│   └── specials.py   # SPECIALS, special_bytecode, etc.
└── lib/              # C extension (native types and runtime)
    ├── *.c, *.h     # atom, pair, namespace, formals, ref, values, bytecode, eval, ...
```

## Where symbols are defined

| Concern | Module | Contents |
|--------|--------|----------|
| Base exceptions | `koskari.exceptions` | `KoskariException`, `NotYetImplemented`, `KoskariSyntaxError` |
| Value types (native) | `koskari.lib` | `symbol`, `keyword`, `pair`, `nil`, `cons`, `car`, `cdr`, `Namespace`, `NamespaceSpec`, `Code`, `OpCode`, `FormalsSpec`, `values`, `Expression`, `Function`, etc. |
| Value types + predicates | `koskari.types` | Imports from `koskari.lib`; defines `TypePredicate`, `BuiltinPredicate`, `is_symbol`, `is_pair`, `build_proper`, `unpack`, `get_position`, etc. |
| Parse errors | `koskari.parse` | `ReaderSyntaxError`, `FormatStringSyntaxError` (subclass `KoskariSyntaxError` from `koskari.exceptions`) |
| Compiler errors | `koskari.compile.exceptions` | `CompilerException`, `UncaughtCompilerException`, `CompilerMode` |
| Parsing | `koskari.parse` | `Reader`, `SourceStream`, `source_str`, `source_open`, `default_reader` |
| Compilation | `koskari.compile` | `Compiler`, `ByteCodeFragment` (and `unique_append` from `koskari.compile.fragment`) |
| Disassembly | `koskari.dis` | `dis`, and internal helpers |

## Import policy

- **Internal koskari code:** Import from the defining module only (e.g. `from koskari.exceptions import KoskariSyntaxError`, `from koskari.types import is_pair`, `from koskari.lib import Code`). Do not use `from koskari import ...` for submodule-defined symbols.
- **Tests:** Same rule: import from `koskari.lib`, `koskari.types`, `koskari.exceptions`, `koskari.parse`, `koskari.compile`, `koskari.dis`, or `koskari.compile.fragment` as appropriate.
- **External programs:** Import from the submodule that defines the symbol. The top-level `koskari` package has no re-exports.

## Test layout

Tests mirror the package by use case:

- **tests/lib/** — Native extension: `test_atom`, `test_pair`, `test_formals`, `test_ref`, `test_namespace`, `test_values`, `test_stack`, `test_eval`, `test_ceval`, `test_utils`, `test_operator`, `test_expression_function`
- **tests/compile/** — Compiler: `test_bytecode` (#bytecode special form), `test_bytecode_fragment` (ByteCodeFragment/Code API)
- **tests/parse/** — Parser: `test_parse`
- **tests/dis/** — Disassembler: `test_dis`
- **tests/types/** — Python type layer: `test_predicates`
- **tests/native/** — C harness and memory tests (unchanged)

Test helpers (e.g. `make_callable`) live in `tests/__init__.py` and import from `koskari.lib` and `koskari.types` as needed.
