# Koskari: import path rework

This document specifies a coordinated rework of the import path: the import
opcode family becomes **self-contained** (EXTRA-style, no stack chaining),
the `import` surface gains a **binding member form** with Python-parity
root/leaf semantics for dotted names, and **wildcard import**
(`import-all-from` / `IMPORT_STAR`) is added.

**See also:** [OPCODES.md](../OPCODES.md) (import opcode family),
[SPECIALS.md](../SPECIALS.md) (`import`, `import-from`),
[api/import-system.md](../api/import-system.md) (module loader),
[MACROS.md](../MACROS.md) (`declare-exports` / `__all__`).

**Status:** Implemented and archived. This note captures the original
design; the shipped reference lives in
[../OPCODES.md §10.1](../OPCODES.md#101-module-import) (opcodes) and
[../SPECIALS.md](../SPECIALS.md) (surface forms), which are authoritative if
they diverge from this note. Superseded and absorbed the earlier
`language/import-all-from.md` (wildcard import), which described only the
star form against the pre-EXTRA opcode shapes.


## Motivation

### The opcodes predate the EXTRA table

The current import opcodes were designed before EXTRA rows existed, and it
shows:

- **`IMPORT_FROM` is the only peek-not-pop opcode in the instruction set.**
  It reads the module at TOS without consuming it, so every sequence of
  `IMPORT_FROM` instructions must be followed by a compensating `POP 1`.
  This is a structural invariant enforced by compiler discipline
  (`helper_import_from_sequence` is the sole permitted emission site) and a
  shouting comment block in the handler — a compiler-side contract papering
  over an opcode-shape problem.
- **`IMPORT_REL` exists only because there was nowhere to put `level`.**
  It is the `level > 0` case of the same
  `PyImport_ImportModuleLevelObject` call as `IMPORT`, carrying an ad-hoc
  `(level:int, name:str|None)` tuple constant that the handler unpacks and
  type-checks at runtime.
- **Member imports cost two instructions each** (`IMPORT_FROM` +
  `VAR_STORE`), so `(import-from foo bar baz)` compiles to 7 instructions.

### The plain dotted import binds the wrong name

`(import foo.bar)` currently binds `bar` to the leaf module. That matches
*neither* Python's plain `import foo.bar` (binds `foo`, the root, with
`foo.bar` reachable as an attribute) *nor* an explicit
`import foo.bar as bar`. It is a silent hybrid that readers of
Python-shaped code will misread. Leaf binding should be an explicit choice,
not a surprise.

### Wildcard import does not exist

Koskari modules using `declare-exports` define `__all__` expecting
consumers to be able to bulk-import public names the Python way, and there
is no surface or opcode for it. Named `import-from` cannot express bulk
binding without listing every export at compile time.


## Opcode design

Three self-contained opcodes replace the current three. Each carries the
module name and relative-import level in its own EXTRA row, imports the
module itself, stores its own bindings, and has **no stack effect and no
dependency on neighboring instructions** — no module chaining, no
peek-not-pop, no compensating pops, no trailing `VAR_STORE`.

```
OP(IMPORT,      EXTRA, CONST, COUNT, COUNT, CONST)
OP(IMPORT_FROM, EXTRA, CONST, COUNT, CONST, CONST)
OP(IMPORT_STAR, EXTRA, CONST, COUNT, NOARG, NOARG)
```

`IMPORT_REL` is deleted; its behavior folds into the `level` field.

### Shared module resolution

All three handlers resolve their module through one static helper
(hosted in `eval.c` alongside `get_name_from_insn` / `frame_ns_store`,
since the opcode files are `#include`d there):

```
kos_import_module(es, name, level, leaf) -> new module ref or NULL
```

- `name` — module name str from the cpool, or None (mapped to NULL) for a
  bare relative import.
- `level` — number of leading dots; 0 = absolute.
- When `level > 0`, build the `__name__` / `__package__` globals dict from
  the frame's globals namespace exactly as `IMPORT_REL` does today.
- `leaf` selects the fromlist passed to
  `PyImport_ImportModuleLevelObject`: the `("*",)` sentinel returns the
  **leaf** module; a NULL fromlist returns the **root** while still
  importing the full dotted chain and attaching submodules as attributes —
  which is precisely Python's plain-import semantics in one call.

### `KOS_OP_IMPORT`

- **Row**: `a0` = cpool name str (or None), `a1` = level, `a2` = leaf flag
  (0 = bind root, 1 = bind leaf), `a3` = cpool binding name str. All four
  slots used.
- **Stack**: no effect.
- Resolve the module per the leaf flag and store it into the current frame
  namespace under the binding name (same store path as `VAR_STORE`). The
  compiler still calls `declare_var` for the binding name.

### `KOS_OP_IMPORT_FROM`

- **Row**: `a0` = cpool name str (or None), `a1` = level, `a2` = cpool
  tuple of attribute names, `a3` = cpool tuple of binding names (parallel,
  same length; the same tuple object when no renames). All four slots used.
- **Stack**: no effect.
- Resolve the leaf module, then for each `attrs[i]`:
  `getattr(module, attrs[i])` and store into the current frame namespace
  under `binds[i]` (same store path as `VAR_STORE`).
- **Submodule fallback** (fixes a current semantic gap): on
  `AttributeError`, attempt to import `"{name}.{attrs[i]}"` as a module
  and retry the getattr, matching CPython's `from pkg import sub` behavior
  for submodules not imported by the package `__init__`. Other exceptions
  propagate.

### `KOS_OP_IMPORT_STAR`

- **Row**: `a0` = cpool name str (or None), `a1` = level.
- **Stack**: no effect.
- Resolve the leaf module, select its public names, and store each into
  the current frame namespace:
  1. If the module defines `__all__` (a list or tuple of strings), bind
     exactly those names; a non-string entry is a `TypeError` at runtime
     (match CPython).
  2. Otherwise take `dir(module)` and bind every name that does not start
     with `_`.
  For each selected name: `getattr(module, name)`, store. Existing
  bindings in the target namespace are overwritten, matching Python.
- Prefer mirroring CPython's `import_all_from` logic rather than
  re-deriving edge cases.


## Surface forms

### `import`

```
(import MODULE)                ; plain: bind the ROOT name
(import [MODULE as: BIND])     ; binding member: bind BIND to the LEAF
```

- **Plain absolute dotted import binds the root**, Python-style:
  `(import foo.bar)` binds `foo`, with `foo.bar` reachable as an
  attribute. Leaf binding requires the explicit member form.
- The bracketed `[MODULE as: BIND]` member reuses the same parser as
  `import-from` members (`helper_using` / `_parse_member`), unifying the
  member grammar across both forms.
- The existing inline `(import MODULE as: BIND)` spelling is retired in
  favor of the bracketed member (one grammar, not two).

**Relative imports are always leaf-style.** Python forbids plain
`import .foo` outright; Koskari keeps it, and there is no root-name
ambiguity to protect against — the anchor package has no name in the
source form. So:

- `(import .foo)` binds `foo` (the leaf), as today.
- `(import [.foo.bar as: fb])` binds `fb` to the leaf.
- Bare relative imports (dots only, no name) require the binding form,
  since there is no name to bind: `(import [.. as: parent])`. Note that a
  **single** lone dot is the reader's dotted-pair marker and cannot be
  written in list syntax at all, so bare relative forms start at two
  dots; `from . import x` is spelled `(import .x)` or
  `(import-from .. x)` from one package up.

### `import-from`

Surface unchanged: `(import-from MODULE MEMBER ...)` with plain-symbol and
`[ATTR as: BIND]` members. Only the emission changes (single
`IMPORT_FROM` instruction).

### `import-all-from`

```
(import-all-from MODULE)
```

Layout (statement position): `import-all-from MODULE`.

- `MODULE` follows the same dotted-name and relative rules as `import` /
  `import-from` (parsed by `_parse_import_name`).
- No member list, no `as:` alias, no mixing with named imports; to combine
  wildcard and named imports, write two statements (same as Python).
- **Module top level only**, matching Python 3. The compiler rejects
  `import-all-from` inside a function, lambda, or class body with a
  `SpecialError` mirroring Python's "import * only allowed at module
  level". Detection mechanism (compiler mode flag vs fragment/resolution
  parent chain) to be settled at implementation time.
- No `declare_var` for wildcard names — they are unknown at compile time;
  module namespaces accept runtime stores.
- Register as `compile/import-all-from` via `@builtin` / `@special`;
  surface keyword `import-all-from`.

### Binding summary

| Form | Binds | Module object |
|---|---|---|
| `(import foo.bar)` | `foo` | root (`foo.bar` reachable as attribute) |
| `(import [foo.bar as: fbar])` | `fbar` | leaf |
| `(import .foo)` | `foo` | leaf (relative; unambiguous) |
| `(import [.foo.bar as: fb])` | `fb` | leaf |
| `(import-from foo.bar x [y as: z])` | `x`, `z` | attributes of leaf |
| `(import-all-from foo.bar)` | leaf's public names | — |


## Emission

All import statements evaluate to `None` (`CONST None`), as today.

```
(import foo.bar)
    IMPORT ("foo.bar", 0, root, "foo") / CONST None

(import [foo.bar as: fbar])
    IMPORT ("foo.bar", 0, leaf, "fbar") / CONST None

(import .foo)
    IMPORT ("foo", 1, leaf, "foo") / CONST None

(import-from foo bar [baz as: bz])
    IMPORT_FROM ("foo", 0, ("bar", "baz"), ("bar", "bz")) / CONST None

(import-all-from os.path)
    IMPORT_STAR ("os.path", 0) / CONST None
```

Every import statement is exactly 2 instructions: one import opcode plus
the `CONST None` statement value. `(import-from foo bar baz)` drops from 7
instructions today to 2. `helper_import_from_sequence` and its
POP-compensation contract are deleted; `import` and `import-from` bindings
still get `declare_var` at compile time.


## Serialization / ABI

- The EXTRA machinery in `bytecode.c`, `codex.py`, and `dis` handles the
  new shapes generically (proven by the `BUILD_FUNCTION` reshape); no new
  serialization code.
- Name strings and names tuples are ordinary cpool constants; `None` for a
  bare relative name uses the existing `NONE` pool kind; `level` and the
  leaf flag are raw COUNT fields.
- One `BYTECODE_ABI_MINOR` bump: `IMPORT` / `IMPORT_FROM` reshape,
  `IMPORT_REL` removal, `IMPORT_STAR` addition.


## Tests (implementation phase)

### Compile (`tests/compile/specials/test_import*.py`)

| Case | Assert |
|------|--------|
| `(import foo.bar)` | `IMPORT` row (name, 0, root, "foo"); no `VAR_STORE` |
| `(import [foo.bar as: fbar])` | `IMPORT` row (name, 0, leaf, "fbar") |
| `(import .foo)` | `IMPORT` row (name, 1, leaf, "foo") |
| `(import-from foo bar [baz as: bz])` | single `IMPORT_FROM` row with parallel tuples; no `POP` |
| `(import-all-from os)` | `IMPORT_STAR` row; `CONST None` |
| `(import-all-from .foo)` | `IMPORT_STAR` row (level 1) |
| `(import-all-from)` / extra args | `SpecialError` |
| `(import-all-from os)` inside lambda | `SpecialError` (module-level guard) |

### Opcode / runtime (`tests/lib/opcode/`)

| Case | Assert |
|------|--------|
| `IMPORT` root vs leaf flag | root binding is the package; leaf binding is the submodule |
| `IMPORT_FROM` multiple members | all bound; renames honored; no stack effect |
| `IMPORT_FROM` submodule fallback | `from pkg import sub` works when `__init__` does not import `sub` |
| `IMPORT_STAR` with `__all__` | only listed names bound; non-string entry raises `TypeError` |
| `IMPORT_STAR` without `__all__` | public `dir()` names bound; `_private` skipped |
| `IMPORT_STAR` overwrite | pre-existing binding updated |
| relative level > 0 | anchor resolution via frame globals |

### Integration

- `examples/scheduler/example.ki` — adopt `import-all-from kosrunner`
  (currently worked around with a named import).
- Corpus records for read/compile/eval phases of all three forms.
- Native opcode smoke if the import family gains native coverage.


## Documentation graduation

When implemented:

1. `design/OPCODES.md` — rewrite the import family sections (§10.x).
2. `design/SPECIALS.md` — `import` binding semantics, member form,
   full `import-all-from` section.
3. `docs/reference/vm/` — per-opcode pages, families table, index section.
4. `docs/reference/specials/` — update `import` / `import-from`; add
   `import-all-from` (no `design/` links per docs boundary rules).
5. `design/FEATURES.md` — import row.
6. Editor token lists (`tools/editor-tokens/spec.yml`, TextMate,
   tree-sitter) — add `import-all-from` keyword.
7. Corpus `docs/corpus/` records as above.

Migration note: the root-binding change for plain dotted imports is a
behavior change for existing `.ki` sources (including bootstrap and
examples); audit `(import x.y)` call sites for leaf-binding assumptions
and convert them to the member form as part of the implementation.


## Open questions

- Multiple members per `import` statement (`(import foo [bar.baz as: bb])`
  like Python's `import a, b`) — falls out nearly free from the member
  grammar; decide whether to allow in v1 or keep one module per statement.
- Exact detection mechanism for the module-level-only guard on
  `import-all-from`.


## Non-goals

- Extending `import-from` with `*` members (wildcard is its own form).
- Wildcard import inside nested scopes.
- Compile-time enumeration of wildcard names for IDE completion.
- Automatic `__all__` generation on import (remains `declare-exports`).
- Changes to the module loader / import hook machinery
  ([api/import-system.md](../api/import-system.md)); this rework is
  opcode- and surface-level only.


# The end.
