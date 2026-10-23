# Native Memory Tests

This directory contains C-level memory tests for Koskari's native types. These tests verify correct reference counting, memory allocation/deallocation, and absence of memory errors like use-after-free, double-free, and buffer overflows.

## Overview

The test harness embeds a Python interpreter and initializes our native types directly, then exercises them through the C API. Tests run under three modes:

| Mode | Purpose | Tool |
|------|---------|------|
| **normal** | Basic correctness, catches crashes and assertion failures | None |
| **asan** | Use-after-free, buffer overflows, double-free | AddressSanitizer |
| **valgrind** | Memory errors, uninitialized reads | Valgrind Memcheck |

## Quick Start

### Using Container (Recommended)

The container provides a consistent environment with all required tools:

```bash
# From project root
make test-native     # Run normal tests
make test-asan       # Run ASan tests
make test-valgrind   # Run Valgrind tests
```

### Running Locally

If you have the required tools installed (clang or GCC 15+ for
`musttail`, valgrind, an ASan runtime):

```bash
# From this directory (use -f tests.mk; no default Makefile)
make -f tests.mk test           # Normal tests
make -f tests.mk test-asan      # ASan tests
make -f tests.mk test-valgrind  # Valgrind tests
```

## File Structure

```
tests/native/
├── README.md              # This file
├── Containerfile          # Container definition (Fedora + tools)
├── tests.mk               # Build and test rules (container entrypoint uses this)
├── valgrind-python.supp   # Valgrind suppressions for Python internals
├── test_harness.h         # Common macros and unit runner declarations
├── test_harness.c         # Main entry point: initializes Python and the library, runs every suite
├── test_arena.c           # Arena and CodeAllocator: reservation, seal, code placement
├── test_atom.c            # Symbol and keyword interning
├── test_breaker.c         # The eval breaker as read through the compat layer
├── test_bytecode.c        # KosCode: instructions, constant pool, position table, read-only protection
├── test_demand.c          # Demand wrapper: kept root frame, BoundMethod vectorcall
├── test_domain.c          # Per-thread record behind the ambient context stack
├── test_dynchain.c        # DynNode chain memory management
├── test_eval.c            # Evaluator: opcode dispatch, OBJECT_ADD, delimited continuations
├── test_formals.c         # Formals and FormalsSpec
├── test_iter_request.c    # Iteration request API and the code-less frame helpers
├── test_iterate.c         # Python adapters over a KosIter provider
├── test_lifecycle.c       # Module clear and re-init over a fresh module object
├── test_matcher.c         # Matcher step helpers through a Datum box
├── test_namespace.c       # Namespace, NamespaceSpec, CollectorNamespace, ModuleNamespace, Module
├── test_pair.c            # Pair free list, head/tail refcounts, cycles, nil singleton
├── test_paramref.c        # ParamRef deref, store, clear, isEmpty with an active domain scope
├── test_partial.c         # KosPartial
├── test_property.c        # KosProperty construction and dealloc
├── test_resume.c          # Per-frame CPS resume plumbing on KosDynFrame
├── test_stack.c           # Operand stack segments
└── test_values.c          # Values type: args/kwds refcounts, cycles
```

## Writing Tests

### Test Macros

Available assertion macros (defined in `test_harness.h`):

```c
TEST_ASSERT(condition, message)           // Assert condition is true
TEST_ASSERT_EQ(actual, expected, message) // Assert equality
TEST_ASSERT_NOT_NULL(ptr, message)        // Assert pointer is not NULL
TEST_ASSERT_NULL(ptr, message)            // Assert pointer is NULL
TEST_ASSERT_REFCNT(obj, expected, message)// Assert reference count
TEST_ASSERT_SAME(a, b, message)           // Assert same object (pointer)
TEST_ASSERT_NOT_SAME(a, b, message)       // Assert different objects
```

### Test Function Pattern

Each test is a function returning 0 on success, non-zero on failure:

```c
static int test_example(void) {
    PyObject *obj = SomeType_New(...);
    TEST_ASSERT_NOT_NULL(obj, "create object");

    // Test operations...
    TEST_ASSERT_EQ(Py_REFCNT(obj), 1, "refcount is 1");

    Py_DECREF(obj);
    return 0;  // Success
}
```

### Running Tests

Use `RUN_TEST()` macro in the suite runner:

```c
int run_mytype_tests(void) {
    BEGIN_TEST_SUITE("MyType Memory Tests");

    RUN_TEST(test_example);
    RUN_TEST(test_another);

    return 0;
}
```

## Evaluator Phase Tests (test_eval.c)

`test_eval.c` exercises `koskari_eval` directly at the C level using manually
constructed bytecode.  Tests are organized into two groups:

**Opcode correctness / error paths** — stack underrun, wrong-type operand,
missing TRY/FINALLY/CC push, domain invariants, delimited continuation capture
and invocation.

**Phase-boundary tests** — cover the entry phase of `koskari_eval` and the
raise ingress:

| Test | Phase exercised |
|------|----------------|
| `test_eval_yield_empty_resume` | phase 1 (`eval_phase_entry` — revive_yield with no send value, resumed with `None`) |
| `test_eval_yield_send_resume` | phase 1 (`eval_phase_entry` — revive_yield with send value) |
| `test_eval_yield_throw_resume` | phase 1 (`eval_phase_entry` — revive_yield with throw, no handler) |
| `test_eval_yield_throw_caught` | phase 1 (`eval_phase_entry` — KOS_MODE_EXCEPTION through `process_raise`, local handler found) |
| `test_eval_opcode_error_caught_by_try` | raise ingress (`process_raise` — an opcode raises, local handler found) |

The helper `make_simple_ctx(instr, &code_obj, &ctx, &gen)` constructs a
minimal nspec + code + frame + context from a Python list of instruction
tuples. For yielding code the frame sits under a standalone `KosDynGen`,
returned through `gen` (NULL otherwise); after a YIELD the test installs
it with `install_frozen_gen` before the next `koskari_eval`.
Note that `kos_dynframe_new` **steals** the `ns` reference; the helper
accounts for this — do not `Py_DECREF(ns)` after passing it.

### Direct dyn-chain calls outside `koskari_eval`

The chain and its live registers live on the context itself, and the
`kos_dyn_*` helpers take the context. The helpers read the context's breaker
and recursion limit, which a drive sets, so native tests that call them
directly bracket the calls the way `koskari_eval` brackets a drive:

1. `KosContext_EvalEnter(ctx, NULL)`
2. mutate the chain (push, pop, gen install, …) passing `ctx`
3. `KosContext_EvalExit(ctx)` before the next `koskari_eval` on `ctx`

Canonical patterns in this tree:

- `install_frozen_gen` in `tests/native/test_eval.c` (mirrors the
  install in `asyncgen_teardown_chain_on_ctx` in `koskari/lib/iter.c`)
- `push_dyn_param` / `pop_dyn_param` in `tests/native/test_paramref.c`

Skipping `KosContext_EvalEnter` on a context that has never been driven
leaves its breaker unset and its recursion limit at zero.

## Adding Tests for a New Type

When adding a new native type (e.g., `values.c`), follow these steps:

### 1. Create the Test File

Create `tests/native/test_values.c`:

```c
/*
  Memory tests for values type
  ...license header...
*/

#include "test_harness.h"
#include "../../koskari/lib/lib.h"

static int test_values_create(void) {
    // Test implementation
    return 0;
}

static int test_values_refcount(void) {
    // Test implementation
    return 0;
}

// ... more tests ...

int run_values_tests(void) {
    BEGIN_TEST_SUITE("Values Memory Tests");

    RUN_TEST(test_values_create);
    RUN_TEST(test_values_refcount);
    // ... more tests ...

    return 0;
}
```

### 2. Declare the Runner in test_harness.h

Add the declaration to the "unit test runners" section:

```c
/* === unit test runners === */

int run_atom_tests(void);
int run_pair_tests(void);
int run_values_tests(void);  // ADD THIS
```

### 3. Call the Runner in test_harness.c

Add the call in `main()`, before `run_lifecycle_tests()`, which stays
last because it clears and rebuilds the statics every other suite uses:

```c
/* Run all test suites */
run_atom_tests();
run_pair_tests();
...
run_values_tests();  // ADD THIS
/* last: it clears and rebuilds the statics every suite above used */
run_lifecycle_tests();
```

### 4. No Makefile update needed

Sources are discovered via globs: test files matching `test_*.c` and library sources matching `koskari/lib/*.c` are picked up automatically (same as setup.py). Adding a new `test_foo.c` and wiring it in `test_harness.h` and `test_harness.c` is sufficient.

### 5. Run Tests

```bash
make test-native
make test-asan
make test-valgrind
```

## Tips for Writing Good Memory Tests

### Test Reference Counting

```c
static int test_type_refcount(void) {
    // Use large integers to avoid Python's small int cache
    PyObject *obj = PyLong_FromLong(100000);
    Py_ssize_t initial = Py_REFCNT(obj);

    PyObject *container = MyType_New(obj);
    TEST_ASSERT_EQ(Py_REFCNT(obj), initial + 1, "container holds ref");

    Py_DECREF(container);
    TEST_ASSERT_EQ(Py_REFCNT(obj), initial, "ref released");

    Py_DECREF(obj);
    return 0;
}
```

### Test Deallocation

```c
static int test_type_dealloc(void) {
    PyObject *obj = MyType_New(...);
    TEST_ASSERT_NOT_NULL(obj, "create object");

    // Release - should not crash
    Py_DECREF(obj);

    // If we get here, deallocation worked
    return 0;
}
```

### Test Cyclic Structures

```c
static int test_cyclic(void) {
    PyObject *a = MyType_New(...);
    PyObject *b = MyType_New(...);

    // Create cycle
    MyType_SetRef(a, b);
    MyType_SetRef(b, a);

    // Release our refs - GC should handle the cycle
    Py_DECREF(a);
    Py_DECREF(b);
    PyGC_Collect();

    return 0;
}
```

### Stress Test Allocations

```c
static int test_stress_alloc(void) {
    PyObject *objects[500];

    // Allocate many
    for (int i = 0; i < 500; i++) {
        objects[i] = MyType_New(...);
        TEST_ASSERT_NOT_NULL(objects[i], "create stress object");
    }

    // Deallocate all
    for (int i = 0; i < 500; i++) {
        Py_DECREF(objects[i]);
    }

    return 0;
}
```

## Notes on Python Memory Behavior

### Small Integer Cache

Python caches small integers (-5 to 256) with very high refcounts. When testing refcount behavior, use large integers (e.g., `PyLong_FromLong(100000)`) to get predictable refcount values.

### ASan and Leak Detection

ASan's leak detector is disabled (`detect_leaks=0`) because Python's allocator produces false positives. The tests focus on detecting:
- Use-after-free
- Buffer overflows
- Double-free
- Stack buffer overflow

### Valgrind Suppressions

Python's internal allocations appear as "leaks" to Valgrind but are expected behavior (type objects, interned strings, module dicts persist for interpreter lifetime). The `valgrind-python.supp` file suppresses these.

We also use the Python allocator: the harness and koskari allocate PyObjects (dicts, lists, tuples, longs, etc.) via PyDict_New, PyList_New, PyTuple_New, and similar. Those allocations go through the same pymalloc and PyDict_*/PyList_*/PyTuple_* paths, so the broad suppressions in `valgrind-python.supp` can hide leaks that are our fault (e.g. missing Py_DECREF). To check for our own leaks, run Valgrind occasionally with the broad pymalloc/dict/list/tuple suppressions commented out, or rely on refcount discipline and Python refcount debugging builds.

## Troubleshooting

### "Module not found" errors

The test harness does not import the Python module. `initialize_types()` in `test_harness.c` runs `koskari_lib_init`, the init `PyInit_lib` runs, over a module of its own; a type must be readied by that init to be available to the suites.

### Segfaults during type initialization

Check that your type's `tp_new`, `tp_dealloc`, `tp_traverse`, and `tp_clear` functions are correctly implemented.

### Refcount assertion failures

Remember that some Python objects (small integers, interned strings, `None`, `True`, `False`) have immortal or cached refcounts that don't follow normal patterns.

### Container build failures

If `make test-container-build` fails with permission errors, ensure you're running with appropriate permissions or use `podman` in rootless mode.
