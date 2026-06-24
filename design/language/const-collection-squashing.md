# Const-run squashing for `#list` and `#tuple`

**Status:** Design proposal — not yet implemented.

**Goal:** When `compile/#list` or `compile/#tuple` literal elements are known at
compile time, fold contiguous constant runs into a single tuple in the constant
pool instead of emitting one `CONST` per element followed by `BUILD_LIST` or
`BUILD_ARGS`.

**Current state:** `compile/#list` and `compile/#tuple` (`koskari/compile/specials/collections.py`) compile every element independently and finish with `BUILD_LIST n` or `BUILD_ARGS n`. Literal ints, strings, and other `_is_const` values still produce `n` separate loads even though the whole run could live in `co_consts` as one tuple.

---

## 1. Problem

Python (and Koskari) treat **tuples of constants** as a single constant-pool
value, but **lists** cannot be stored in the pool because they are mutable.
A list literal made only of constants is still built dynamically at runtime from
its elements.

Today Koskari encodes that limitation literally: `(#list 1 2 3)` becomes three
`CONST` opcodes plus `BUILD_LIST 3`. The same information could be carried as
one tuple constant plus a short construction sequence.

`CONST_CONTAINER_TYPES` in `koskari/compile/compiler.py` already allows tuples
(and frozensets) in the pool when every element is const. Lists are excluded by
design.

---

## 2. Reference: CPython 3.11+

All-constant list displays use:

```
BUILD_LIST 0
LOAD_CONST   (tuple of elements)
LIST_EXTEND  1
```

The tuple is the shared immutable constant; each evaluation allocates a fresh
list and extends it from that tuple.

---

## 3. Proposed primitives

### 3.1 `KOS_OP_BUILD_LIST_EXTEND` (new)

Mirror `LIST_EXTEND`: stack `[..., list, iterable]` → `[..., list]` with the
list extended in place and the iterable popped.

For `PyTuple_CheckExact` iterables, prefer a tight copy loop (same work a fused
`BUILD_CONST_LIST` would do). `BUILD_LIST_APPEND` remains for single dynamic
elements.

Register in `opcodes.incl` with an appropriate style (likely `COUNT` with
argument `1`, matching Python).

### 3.2 Shared compiler helper: `partition_const_runs`

Add to `koskari/compile/helpers.py` (name tentative):

```python
def partition_const_runs(elements):
    """
    Yield ('const', tuple_of_values) or ('expr', source_form) chunks
    """
```

Use `compiler._is_const` / `_is_const_container` rules: only squash elements
that are already valid constant-pool material. Merge adjacent const elements into
one tuple per run before emission.

---

## 4. Emission strategy

### 4.1 `compile/#tuple`

| Shape | Emission |
|-------|----------|
| empty | `BUILD_ARGS 0` (or `CONST ()` — pick one canonical form) |
| single const run (all elements const) | `CONST (e0, …, e{n-1})` only |
| mixed const runs and expressions | per chunk: const → `CONST tuple`; expr → compile + `BUILD_ARGS 1`; then `COMBINE_ARGS` with chunk count |
| all dynamic, no const runs | `BUILD_ARGS n` (current behaviour) |

`COMBINE_ARGS` already passes a lone tuple through without copy when count is 1.
Multiple chunks use the existing concat path in `COMBINE_ARGS.c`.

### 4.2 `compile/#list`

| Shape | Emission |
|-------|----------|
| empty | `BUILD_LIST 0` |
| all const, one element | `CONST x`, `BUILD_LIST 1` (two opcodes; cheaper than extend) |
| all const, two or more | `BUILD_LIST 0`, `CONST tuple`, `BUILD_LIST_EXTEND 1` |
| mixed | `BUILD_LIST 0`; const run → `CONST` + `BUILD_LIST_EXTEND 1`; expr → compile + `BUILD_LIST_APPEND` |
| all dynamic, no const runs | `BUILD_LIST n` (current behaviour) |

Each evaluation must produce a **new** list. Never place a list in the constant
pool.

---

## 5. Optional peephole folds (`compress()`)

After chunk emission works in the specials, `Compiler.compress()` may fold:

- `CONST t`, `COMBINE_ARGS 1` → `CONST t`
- `BUILD_LIST 0`, `CONST t`, `BUILD_LIST_EXTEND 1` → fused opcode (only if a
  dedicated `BUILD_CONST_LIST` is added later for size; not required for v1)

Adjacent const runs should be merged in `partition_const_runs`, not left for
the peephole.

---

## 6. Parallels elsewhere

`compile/call` already partitions positional arguments into literal runs
(`BUILD_ARGS`) and splat expressions, combining with `COMBINE_ARGS` when
needed (`koskari/compile/specials/call.py`). Const-run squashing for
collection literals is the same idea applied to `#list` / `#tuple` element
lists.

---

## 7. Tests (when implemented)

- Opcode tests for `BUILD_LIST_EXTEND` (empty extend, tuple const, type errors).
- Compile specials: `(#list 1 2 3)` emits extend pattern, not three `CONST`s +
  `BUILD_LIST 3`.
- `(#tuple 1 2 3)` emits single `CONST (1, 2, 3)`.
- Mixed forms: `(#list 1 x 2)`, `(#tuple 1 x 2)` — correct values, chunk
  opcodes present.
- Pure-dynamic forms unchanged (`BUILD_LIST n` / `BUILD_ARGS n`).

---

## 8. Out of scope (v1)

- `BUILD_CONST_LIST` as a dedicated fused opcode (optional later peephole).
- `BUILD_CONST_SET` / frozenset squashing for `#set`.
- List or tuple **splat** in collection literals (`*:`, unquote-splicing).

# The end.
