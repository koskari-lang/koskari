# Koskari: `import-all-from` and `IMPORT_STAR`

This document specifies wildcard module import: the `import-all-from` special
form and the `IMPORT_STAR` opcode that implements Python's
`from MODULE import *` semantics at the bytecode level.

**See also:** [OPCODES.md](../OPCODES.md) (import opcode family),
[SPECIALS.md](../SPECIALS.md) (`import`, `import-from`),
[api/import-system.md](../api/import-system.md) (module loader),
[MACROS.md](../MACROS.md) (`declare-exports` / `__all__`).

**Status:** Design only — not implemented.


## Goal

Provide a dedicated wildcard import path parallel to the existing named-member
`import-from` special. Wildcard import is a distinct surface form and opcode;
`import-from` is not extended with `*` members.

Python equivalent:

```python
from MODULE import *
```

Koskari surface:

```koskari
(import-all-from MODULE)
```

Relative forms use the same leading-dot convention as `import` and
`import-from`.


## Motivation

Several call sites already assume wildcard import exists:

- `examples/scheduler/example.ki` uses layout syntax `import-from kosrunner *`,
  which is not valid today (`*` is parsed as a symbol and would compile to a
  named attribute lookup for `"*"`).
- Koskari modules that use `declare-exports` define `__all__` expecting
  consumers to re-export or bulk-import public names the Python way.

Named `import-from` cannot express bulk binding without listing every export at
compile time. A single runtime opcode keeps the compiler simple and matches
CPython's `import_all_from` model.


## Surface form

### `import-all-from`

S-expression:

```
(import-all-from MODULE)
```

Layout (statement position):

```
import-all-from MODULE
```

`MODULE` is an unquoted symbol with the same dotted-name and relative-import
rules as `import` / `import-from` (parsed by `_parse_import_name` in
`koskari/compile/specials/imports.py`).

Examples:

```koskari
(import-all-from os)
(import-all-from os.path)       ; wildcard from leaf module os.path
(import-all-from .util)         ; relative
(import-all-from ..parent.sub)  ; two-dot relative
```

There is no member list, no `as:` alias, and no mixing with named imports in
one form. To import some names explicitly and others via wildcard, emit two
statements (same as Python).

### Bootstrap / compile symbol

Register as `compile/import-all-from` via `@builtin` / `@special`, consistent
with `compile/import` and `compile/import-from`. Surface keyword:
`import-all-from`.


## Semantic contract

Wildcard import binds **public names** from the imported module into the
**current frame namespace** using the same name-selection rules as CPython 3
(`import_all_from` in CPython's import machinery).

### Name selection

Given module object `M`:

1. If `M` defines `__all__` (and it is a `list` or `tuple` of strings), bind
   only those names. Non-string entries are a `TypeError` at runtime (match
   CPython).
2. Otherwise, take `dir(M)`, keep names that do not start with `_`, bind each.

For each selected name `n`:

- `val = getattr(M, n)` (attribute access, not sub-module import).
- Store `n → val` into the current frame namespace.

### Overwriting

Existing bindings in the target namespace are overwritten, matching Python
module-level `import *` behavior.

### Statement value

The form evaluates to `None` (`CONST None`), like `import` and `import-from`.

### Scope restriction

Wildcard import is permitted **only at module top level**, matching Python 3:

```koskari
(define f []:
  (import-all-from os))   ; compile error
```

The compiler must reject `import-all-from` when the enclosing compile context
is a function, lambda, class, or other non-module body. Detection strategy:
consult the compiler's namespace / fragment parent chain (module-init fragment
vs nested `FunctionSpec` / `lambda` body). Exact helper TBD at implementation
time; the error class is `SpecialError` with a clear message mirroring Python's
`import * only allowed at module level`.


## Relationship to `_import_fromlist`

The existing `IMPORT` and `IMPORT_REL` opcodes pass a module-level sentinel
`_import_fromlist = ("*",)` to `PyImport_ImportModuleLevelObject` so **dotted
module imports** return the leaf submodule. That sentinel is unrelated to
wildcard name binding.

`IMPORT_STAR` operates on an already-imported module object on the stack. It
does not call `PyImport_ImportModuleLevelObject` with a wildcard fromlist for
binding purposes.


## Bytecode / opcode

### `IMPORT_STAR`

Add to `koskari/lib/opcodes.incl` in the import family (after `IMPORT_FROM`):

```
OP(IMPORT_STAR, NOARG)
```

| Field | Value |
|-------|-------|
| Style | NOARG |
| Stack in | `[..., module]` |
| Stack out | `[...]` |
| Consumes TOS | Yes — module object is popped |

**Behaviour:**

1. Pop module from TOS. Raise if TOS is missing or not a suitable module
   object (any object with attribute lookup semantics; typically a
   `types.ModuleType` or koskari `Module` wrapper exposing attributes the same
   way).
2. Compute the export name list per the name-selection rules above.
3. For each export name, `getattr(module, name)` and store into the current
   frame namespace via `KosNamespace_Store` / `frame_ns_store` (same storage
   path as `VAR_STORE` used by `import-from`, not `VAR_BIND`).
4. On attribute error or store error, abort with Python exception propagated
   through the evaluator raise path.
5. Return `EVAL_NORMAL` with stack unchanged apart from the popped module.

**Implementation notes:**

- Prefer mirroring CPython's `import_all_from` logic (or calling the same
  internal helper if exposed and stable) rather than re-deriving edge cases.
- Run `make handlers` / `tox -e handlers` after editing `opcodes.incl`.
- Handler file: `koskari/lib/opcode/IMPORT_STAR.c`.
- No constant-pool argument.

### Opcode sequence

Absolute:

```
IMPORT "mod.dotted"
IMPORT_STAR
CONST None
```

Relative:

```
IMPORT_REL (level, "name")
IMPORT_STAR
CONST None
```

Unlike `IMPORT_FROM`, no trailing `POP 1` is required: `IMPORT_STAR` consumes
the module.


## Compiler special

### Handler

Add `special_import_all_from` in `koskari/compile/specials/imports.py` (same
module as the other import specials).

### Parsing

```
(import-all-from NAME)
```

- `NAME` — required module symbol (with optional leading dots).
- No further arguments. Extra arguments → `SpecialError`.
- Reuse `_parse_import_name` for absolute vs relative split.

### Emission

```text
fragment:
  IMPORT or IMPORT_REL   ; push module
  IMPORT_STAR            ; pop module, bind all public names
  CONST None             ; statement value
```

### `declare_var`

Do **not** call `declare_var` for individual wildcard names — they are unknown
at compile time. Pragma / annotation registration for bulk import is out of
scope; module namespaces accept runtime stores.

### Helper export

Optional `helper_import_all_from_sequence(frag, import_frag)` mirroring
`helper_import_from_sequence` for symmetry and a single opcode emission site.
If added, `IMPORT_STAR` must only be emitted from that helper (same structural
discipline as `IMPORT_FROM`).


## Disassembly / introspection

- `koskari.dis` / `dis_ng.ki`: show `IMPORT_STAR` with no argument.
- Add `docs/reference/vm/opcodes/kos-op-import-star.md` when graduating to user
  docs.
- Add §10.x section to `design/OPCODES.md` alongside `IMPORT_FROM`.


## Tests (implementation phase)

### Compile (`tests/compile/specials/test_import.py` or sibling)

| Case | Assert |
|------|--------|
| `(import-all-from os)` | `IMPORT "os"`, `IMPORT_STAR`, `CONST None`; no `IMPORT_FROM` |
| `(import-all-from .foo)` | `IMPORT_REL`, `IMPORT_STAR`, `CONST None` |
| `(import-all-from)` | `SpecialError` |
| `(import-all-from os extra)` | `SpecialError` |
| `(import-all-from os)` inside lambda | `SpecialError` (module-level guard) |

### Opcode / runtime (`tests/lib/opcode/test_import_star.py`)

| Case | Assert |
|------|--------|
| Module with `__all__` | Only listed names bound |
| Module without `__all__` | Public `dir()` names bound; `_private` skipped |
| Stack balance | Module popped; no leak |
| Overwrite | Pre-existing name updated |

### Integration

- Fix `examples/scheduler/example.ki` to use `import-all-from kosrunner`.
- Corpus record under `docs/corpus/records/` for read/compile/eval phases.

### Native

- `tests/native/` opcode smoke if other import opcodes have native coverage.


## Documentation graduation

When implemented:

1. `design/SPECIALS.md` — full `import-all-from` section.
2. `docs/reference/specials/import-all-from.md` — user-facing page (standalone;
   no `design/` links per docs boundary rules).
3. `design/language/special-forms.md` — add bullet under Import System.
4. `design/api/import-system.md` — mention third compiler special and opcode.
5. `design/FEATURES.md` — add `import-all-from` to import row.
6. Editor token lists (`tools/editor-tokens/spec.yml`, TextMate, tree-sitter)
   — add `import-all-from` keyword.


## Implementation checklist

1. `opcodes.incl` — `IMPORT_STAR`
2. `koskari/lib/opcode/IMPORT_STAR.c` — handler
3. `make handlers`
4. `koskari/lib/eval.c` — no new sentinel; handler only
5. `koskari/compile/specials/imports.py` — `special_import_all_from`
6. `koskari/compile/specials/__init__.py` — export
7. Bootstrap registration if required by import path for specials
8. Tests (compile, opcode, integration)
9. Fix `examples/scheduler/example.ki`
10. OPCODES.md, SPECIALS.md, user docs, corpus


## Non-goals

- Extending `import-from` with `*` members.
- `from MODULE import * as ALIAS` (not valid Python).
- Wildcard import inside nested scopes.
- Compile-time enumeration of imported names for IDE completion.
- Automatic `__all__` generation on import (that remains `declare-exports`).


# The end.
