# Const-run squashing for `#list` and `#tuple`

**Status:** Implemented (archived).

**Goal:** When `compile/#list` or `compile/#tuple` literal elements are known at
compile time, fold contiguous constant runs into a single tuple in the constant
pool instead of emitting one `CONST` per element followed by `BUILD_LIST` or
`BUILD_ARGS`.

---

## 1. Problem

Python (and Koskari) treat **tuples of constants** as a single constant-pool
value, but **lists** cannot be stored in the pool because they are mutable.
A list literal made only of constants is still built dynamically at runtime from
its elements.

Formerly Koskari encoded that limitation literally: `(#list 1 2 3)` became three
`CONST` opcodes plus `BUILD_LIST 3`. The same information now travels as one
tuple constant plus a short construction sequence.

`CONST_CONTAINER_TYPES` in `koskari/compile/compiler.py` allows tuples (and
frozensets) in the pool when every element is const. Lists are excluded by
design.

---

## 2. Constant detection

Elements are classified by compiling each one and inspecting the resulting
fragment with `const_only_expr` (`koskari/compile/helpers.py`): an element counts
as constant only when it reduces to a single `CONST` whose value is valid
constant-pool material (`_is_const`). This is a superset of source-form
inspection -- it also folds quoted constants and constant-folded expressions --
and it naturally excludes bare symbols, which compile to variable loads.

`partition_const_runs(compiler, elements)` (`koskari/compile/helpers.py`) drives
this: it compiles each element once and yields chunks in source order, merging
adjacent constants into one tuple and gathering adjacent expressions into one
list of fragments:

```
("const", (v0, v1, ...))       # merged constant run, poolable as a tuple
("expr", [frag0, frag1, ...])  # dynamic-expression run, each leaves one value
```

---

## 3. Opcodes

Lists accumulate into a single mutable result; tuples combine via the existing
`COMBINE_ARGS` path (tuples are immutable, so there is nothing to accumulate
into).

| Opcode | Style | Role |
|--------|-------|------|
| `BUILD_LIST_CONST` | `CONST` | pooled sequence -> a **fresh** list, pushed. Seeds the accumulator; the whole all-const case. |
| `BUILD_LIST_EXTEND_CONST` | `CONST` | extend the TOS list in place from a pooled sequence. |
| `BUILD_LIST_EXTEND` | `NOARG` | extend the TOS list in place from a runtime iterable on TOS (`*:` splat segment). Any iterable the list API accepts; driven by `PySequence_InPlaceConcat`. |
| `BUILD_LIST_APPEND` | `COUNT` | append the top N stack values to the list at depth N, in place. (Widened from the former NOARG append-one; the `#list-append` special now emits count `1`.) |
| `BUILD_LIST_APPEND_CONST` | `CONST` | append one pooled value to the TOS list in place. |
| `BUILD_LIST` | `COUNT` | existing -- new list from N stack values (dynamic seed / all-dynamic). |
| `COMBINE_ARGS` | `COUNT` | existing -- concatenate N stack sequences into one tuple. |

`BUILD_LIST_CONST` and every in-place op only ever touch a list an earlier
`BUILD_LIST*` just minted, so the list is uniquely owned and safe to mutate.
Each evaluation of `BUILD_LIST_CONST` allocates a new list, so the mutable
result never aliases the immutable pool constant.

### `BUILD_LIST_JOIN` -- dropped

An earlier draft proposed a `BUILD_LIST_JOIN N` that pops N stack sequences and
pushes one new joined list (the list analogue of `COMBINE_ARGS`), intended for
splat. The in-place accumulator makes it unnecessary: a `*:` splat segment is an
in-place `BUILD_LIST_EXTEND` of the seeded list, so there is never a need to
build N sequences and join them. `JOIN` was never implemented.

---

## 4. Emission

Both specials read their body with `gather_parameters`. When the body carries
any keyword or `**:` argument (`kwds_seq` is non-empty), the special defers to
the runtime builtin via `call_form_reroute` -> `(compile/call CALLED_BY . BODY)`,
preserving the builtin's argument handling and error messages. Otherwise the
positional / `*:` half (`args_seq`) is grouped by `partition_collection_runs`
into `const`, `expr`, and `splat` chunks and emitted inline.

### 4.1 `compile/#tuple`

| Shape | Emission |
|-------|----------|
| empty | `BUILD_ARGS 0` |
| single const run (all elements const) | `CONST (e0, ..., e{n-1})` |
| single dynamic run | compile elements + `BUILD_ARGS n` |
| lone `*:` splat | compile iterable + `COMBINE_ARGS 1` (coerce to a fresh tuple) |
| mixed const runs, expressions, and splats | per chunk: const -> `CONST tuple`; expr run of k -> compile + `BUILD_ARGS k`; splat -> compile iterable (left as-is); then `COMBINE_ARGS` over the chunk count |

`COMBINE_ARGS` passes a lone tuple through when the count is 1, so single const /
expr chunks need no combine; a lone splat still combines so the iterable is
copied into a fresh tuple rather than aliased.

### 4.2 `compile/#list`

| Shape | Emission |
|-------|----------|
| empty | `BUILD_LIST 0` |
| all const | `BUILD_LIST_CONST (e0, ..., e{n-1})` |
| leading const run (mixed) | `BUILD_LIST_CONST run` seed, then the tail |
| leading dynamic run (mixed) | compile + `BUILD_LIST k` seed, then the tail |
| leading `*:` splat | `BUILD_LIST 0` + compile iterable + `BUILD_LIST_EXTEND` seed, then the tail |
| tail const run, length >= 2 | `BUILD_LIST_EXTEND_CONST run` |
| tail const run, length 1 | `BUILD_LIST_APPEND_CONST value` |
| tail dynamic run of k | compile k elements + `BUILD_LIST_APPEND k` |
| tail `*:` splat | compile iterable + `BUILD_LIST_EXTEND` |

Each evaluation produces a **new** list. A list is never placed in the constant
pool.

Worked example -- `(#list 1 2 3 a b 4 5 6 z 8 9)`:

```
BUILD_LIST_CONST (1 2 3)
VAR a / VAR b / BUILD_LIST_APPEND 2
BUILD_LIST_EXTEND_CONST (4 5 6)
VAR z / BUILD_LIST_APPEND 1
BUILD_LIST_EXTEND_CONST (8 9)
```

One list allocation grown in place, two pooled tuples with no runtime cost, zero
transient allocations, max stack depth three.

Splat example -- `(#list 1 2 3 *: a)`:

```
BUILD_LIST_CONST (1 2 3)
VAR a / BUILD_LIST_EXTEND
```

---

## 5. Parallels elsewhere

`compile/call` partitions positional arguments into literal runs (`BUILD_ARGS`)
and splat expressions, combining with `COMBINE_ARGS` when needed
(`koskari/compile/specials/call.py`). The `#tuple` path is the same idea; the
`#list` path diverges because a mutable list accumulates in place rather than
combining immutable sequences.

---

## 6. Tests

- Opcode tests: `tests/lib/opcode/test_list_opcodes.py`
  (`BUILD_LIST_CONST`, `BUILD_LIST_EXTEND_CONST`, `BUILD_LIST_EXTEND`,
  `BUILD_LIST_APPEND_CONST`, and `BUILD_LIST_APPEND` with a count, including
  type errors, arbitrary-iterable extend, and fresh-list-per-eval).
- Compile specials: `tests/compile/specials/test_collections.py`
  (all-const folds, mixed accumulator/combine, pure-dynamic unchanged, the
  worked example, leading/interior/trailing `*:` splat for both `#list` and
  `#tuple`, fresh-copy splat, and keyword reroute).

---

## 7. Out of scope

- `BUILD_CONST_SET` / frozenset squashing for `#set`.

# The end.
