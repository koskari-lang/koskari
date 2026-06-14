# Package layout and imports

Koskari is organized by use case. Internal code and tests import only from the **direct definition location** for each symbol. The top-level package does not re-export; any future re-exports would be for external (non-koskari) convenience only.

## Package structure

```
koskari/
├── __init__.py       # Minimal: docstring only, no re-exports
├── exceptions.py     # All exceptions: Koskari*, Reader*, Layout*, Compiler*, etc.
├── types.py          # Value types (from lib) + Python predicates/helpers
├── reader/           # Parser, layout block syntax, reader macros
│   ├── __init__.py
│   ├── parser.py     # Reader, SourceStream, reader macros, parse errors
│   └── layouts/      # Layout decorator, LAYOUTS collector
├── dis.py            # Disassembler for Code objects
├── compile/          # Compiler, ByteCodeFragment, special forms
│   ├── __init__.py
│   ├── compiler.py   # Compiler, ByteCodeFragment
│   ├── helpers.py # gather_formals, gather_parameters, helper_begin, …
│   └── specials/     # Special form handlers (conditionals, imports, etc.)
├── importer/         # Import hooks for .ki files
│   ├── importlib.py  # KoskariPathFinder, KoskariSourceFileLoader
│   ├── module.py     # new_module, load_module_async
│   └── bootstrap.py  # make_builtins
├── launcher.py       # Installed-script runtime: run_resolved_script, hosted_run
├── cli/              # koskari console entry (REPL, run file, compile-kio)
├── extras/
│   └── setuptools/   # koskari_scripts build plugin (metadata.py, plugin.py)
└── lib/              # C extension (native types and runtime)
    ├── *.c, *.h     # atom, pair, namespace, formals, ref, values, bytecode, eval, ...
```

## Where symbols are defined

| Concern | Module | Contents |
|--------|--------|----------|
| Base exceptions | `koskari.exceptions` | `KoskariException`, `NotYetImplemented`, `KoskariSyntaxError` |
| Value types (native) | `koskari.lib` | `symbol`, `keyword`, `pair`, `nil`, `cons`, `car`, `cdr`, `Namespace`, `NamespaceSpec`, `Code`, `OpCode`, `FormalsSpec`, `values`, `Expression`, `Function`, etc. |
| Value types + predicates | `koskari.types` | Imports from `koskari.lib`; defines `TypePredicate`, `BuiltinPredicate`, `is_symbol`, `is_pair`, `build_proper`, `unpack`, `get_position`, etc. |
| Parse errors | `koskari.exceptions` | `ReaderError`, `ReaderSyntaxError`, `FormatStringSyntaxError`, `LayoutError` (subclass `KoskariSyntaxError`) |
| Compiler errors | `koskari.exceptions` | `CompilerError`, `SpecialError`, `ParametersError`, `UncaughtCompilerError` |
| Parsing | `koskari.reader` | `Reader`, `LayoutReader`, `SourceStream`, `source_str`, `source_open`, `default_reader` |
| Compilation | `koskari.compile` | `Compiler`, `ByteCodeFragment`, `SPECIALS` (from compiler.py, specials/) |
| Disassembly | `koskari.dis` | `dis`, and internal helpers |
| Installed scripts | `koskari.launcher` | `run_resolved_script`, `run_target`, `hosted_run`, `drive_async_loop` |
| Packaging plugin | `koskari.extras.setuptools` | `configure_koskari_scripts`, `validate_koskari_scripts` (`metadata.py`, `plugin.py`) |

## Import policy

- **Internal koskari code:** Import from the defining module only (e.g. `from koskari.exceptions import KoskariSyntaxError`, `from koskari.types import is_pair`, `from koskari.lib import Code`). Do not use `from koskari import ...` for submodule-defined symbols.
- **Tests:** Same rule: import from `koskari.lib`, `koskari.types`, `koskari.exceptions`, `koskari.reader`, `koskari.compile`, or `koskari.dis` as appropriate.
- **External programs:** Import from the submodule that defines the symbol. The top-level `koskari` package has no re-exports.

## Test layout

Tests mirror the package by use case:

- **tests/lib/** — Native extension: `test_atom`, `test_pair`, `test_formals`, `test_ref`, `test_namespace`, `test_values`, `test_stack`, `test_eval`, `test_ceval`, `test_utils`, `test_operator`, `test_expression_function`; `lib/opcode/` for opcode-specific tests (with, wind, yield_from, etc.)
- **tests/compile/** — Compiler: `test_bytecode` (#bytecode special form), `test_bytecode_fragment` (ByteCodeFragment/Code API), `specials/` for special form tests
- **tests/reader/** — Parser: `test_parse`, `test_parse_ng`, `test_layout`; `layouts/` for layout handler tests
- **tests/dis/** — Disassembler: `test_dis`
- **tests/types/** — Python type layer: `test_predicates`
- **tests/importer/** — Import system: `test_basics`, `test_importlib`, `test_module`
- **tests/lexer/** — Lexer and LSP tests
- **tests/matcher/** — (removed) legacy Python matcher tests; see **tests/** for `matcher` coverage
- **tests/cli/** — CLI tests
- **tests/extras/setuptools/** — `koskari_scripts` packaging plugin and launcher tests
- **tests/native/** — C harness and memory tests

Test helpers (e.g. `make_callable`) live in `tests/__init__.py` and import from `koskari.lib` and `koskari.types` as needed.
