# Valgrind Memory Leak Report — Native Tests

This report summarizes the memory leaks reported by Valgrind when running the native test harness (`make test-valgrind` or equivalent). The run used `valgrind.results` (single process, PID 158).

**Last updated:** After applying Py_Finalize in test harness and KosCode faux cache clear/traverse (tp_clear, tp_dealloc, tp_traverse).

---

## Executive summary

| Category | Bytes | Blocks | Notes |
|----------|-------|--------|--------|
| **Definitely lost** | 102,632 | 1,971 | See root causes below |
| **Indirectly lost** | 128 | 2 | Two distinct koskari paths (see §1.1, §1.5) |
| **Possibly lost** | 3,033 | 54 | Lower priority; often false positives |
| **Still reachable** | 0 | 0 | — |
| **Suppressed** | 44,812 | 509 | Existing suppressions (Python, etc.) |
| **ERROR SUMMARY** | 910 errors (26 suppressed) | | |

**Change from previous run:** Definitely lost −2,200 bytes / −11 blocks; indirectly lost −385 bytes / −11 blocks (KosCode faux cache fix); 11 fewer error contexts.

---

## 1. Koskari-originated leaks (actionable)

### 1.1 ~~Faux code object in exception traceback~~ — FIXED

- **Status:** Fixed. `KosCode` now clears and decrefs `KosCode_FAUX(self)` in `tp_clear` and `tp_dealloc`, and visits it in `tp_traverse`.
- **Previous:** 11 blocks × 35 bytes indirectly lost from `KosCode_Faux` → `inject_eval_frame_traceback`. No longer present in current results.

### 1.2 Bytecode module init — `create_int_enum` / `create_opcode_enum` (definitely lost)

- **Pattern:** `PyUnicode_New` (and related) → … → `PyImport_Import` / `PyImport_ImportModule` → `create_int_enum` (bytecode.c:1734) → `create_opcode_enum` (bytecode.c:1786) → `koskari_bytecode_init` (bytecode.c:1829) → `initialize_types` (test_harness.c:110).
- **Size:** Many small blocks (43–46 bytes typical). A large share of the 1,971 “definitely lost” blocks mention this call chain. Largest single record remains **20,103 bytes in 410 blocks** (Py_Initialize*; see §2.1).
- **Cause:** During module init, `create_opcode_enum` calls `create_int_enum("OpCode", enum_dict)`, which does `PyImport_ImportModule("enum")` and then `PyObject_CallMethod(enum_module, "IntEnum", "sO", name, enum_dict)`. The import and IntEnum creation leave allocations that are not freed before exit. `Py_Finalize()` is now called; remaining losses are likely from Python’s import/type machinery that it does not fully release, or from our code not DECREF’ing temporaries.
- **Fix:** See Fix plan §A.

### 1.3 Sync module init (definitely lost)

- **sync.c:654:** 52 bytes in 1 block — `PyDict_SetItemString` from `koskari_sync_init` → `initialize_types` (test_harness.c:150).
- **sync.c:53 (register_koskari_sync_exception):** 61 bytes in 1 block — `PyModule_AddObject` from `register_koskari_sync_exception` → `koskari_sync_init` → `initialize_types`.
- **Cause:** Dict or module attribute allocations not released by teardown. With `Py_Finalize()` in place, these may be Python’s view of module-owned memory that is not fully freed by `Py_Finalize`, or a missing DECREF in our init.
- **Fix:** See Fix plan §B.

### 1.4 Formals module init and tests (definitely lost)

- **test_formals.c:377** — 32 bytes in 1 block: `test_formals_apply_with_defaults`.
- **test_formals.c:206** — 44 bytes in 1 block: `test_formals_create_full` (PyDict_SetItemString).
- **formals.c:1438** — 48 bytes in 1 block: `koskari_formals_init` (PyDict_SetItemString).
- **formals.c:1441** — 52 bytes in 1 block: `koskari_formals_init` (PyDict_SetItemString).
- **Cause:** Formals tests or formals module init create objects (e.g. dict keys/values) that are not DECREF’d or are stored in structures that are never torn down.
- **Fix:** See Fix plan §C.

### 1.5 Remaining indirectly lost (2 blocks, 128 bytes)

- **Block 1 (64 bytes):** `PyDict_New` → `PyObject_SetAttrString` → `decorate_exception_with_dynchain` (eval.c:380) → `inject_eval_frame_traceback` (eval.c:407) → … → `test_eval_call_continuation_underrun`. The dict (or an object set as attribute) attached to the exception for the dynchain is not released when the exception is discarded.
- **Block 2 (64 bytes):** `_PyObject_New` → `KosFormalsSpec_New` (formals.c:231) → `test_eval_build_function_no_defaults` (test_eval.c:1272). A FormalsSpec (or something it owns) is not freed after the test.
- **Fix:** See Fix plan §D, §E.

---

## 2. Test harness / Python interpreter init (definitely lost)

### 2.1 Python initialization — `initialize_python` (test_harness.c:42)

- **Pattern:** Large loss records (e.g. **20,103 bytes in 410 blocks**) with stack ending in `Py_InitializeFromConfig` / `Py_InitializeEx` → `initialize_python` (test_harness.c:42) → main. Also 7,514 bytes in 135 blocks (Py_InitializeFromConfig), 2,735 bytes in 52 blocks (codec/import during init).
- **Cause:** Python interpreter startup allocates objects (interned strings, type objects, codec lookup, import machinery). The test harness now calls `Py_Finalize()` in `cleanup()` before exit; remaining “definitely lost” are allocations that the Python C API does not free at finalization (known behavior on some platforms/builds).
- **Fix:** See Fix plan §F (suppressions for known init leaks if we want a clean Valgrind summary).

---

## 3. Possibly lost (3,033 bytes in 54 blocks)

Valgrind reports “possibly lost” when it finds pointers that might be interior to a block. These are often false positives (e.g. Python’s internal pointers). No distinct koskari-only stack was identified; lower priority. If needed, re-run with `--show-leak-kinds=all` and inspect individual records.

---

## 4. Fix plan for remaining leaks

Apply in order; re-run `make test-valgrind` after each and compare LEAK SUMMARY.

### A. Bytecode init — create_int_enum / create_opcode_enum

1. **Audit reference ownership** in `create_int_enum` (bytecode.c): `PyImport_ImportModule("enum")` returns a new ref; it is DECREF’d. `PyObject_CallMethod(..., "IntEnum", "sO", name, enum_dict)` consumes `enum_dict` (per IntEnum semantics); confirm `enum_dict` is not double-freed or leaked.
2. **In create_opcode_enum**, after building the OpCode enum, the loop that does `PyObject_GetAttrString(res, "KOS_OP_...")` and `PyObject_SetAttrString(_en, "style", _fl)` gets new refs from GetAttr and FromLong; confirm every new ref is DECREF’d (or transferred) and that no temporary dict/object is left unreleased.
3. If leaks remain and are from Python’s `PyImport_ImportModule` / `IntEnum` internals, consider adding a **Valgrind suppression** for this call chain (loss in create_int_enum / create_opcode_enum) so our report focuses on other leaks.

### B. Sync init — koskari_sync_init (sync.c:654), register_koskari_sync_exception (sync.c:53)

1. **sync.c:654:** Inspect the call to `PyDict_SetItemString` (and surrounding code). Ensure the key and value passed are either borrowed or DECREF’d after the call as appropriate. If the dict is module-owned and released by `Py_Finalize`, the leak may be in Python’s module teardown; otherwise add DECREF or clear in module clear.
2. **sync.c:53 (register_koskari_sync_exception):** `PyModule_AddObject` steals the reference. Ensure the object we pass is a new reference we own; do not DECREF it after the call. The 61-byte loss may be an internal allocation of the type we register; if so, consider a suppression or ensure the type is fully finalized.

### C. Formals tests and formals init

1. **test_formals.c:377 (test_formals_apply_with_defaults):** Trace all Python API calls (e.g. formals apply with defaults). Ensure every new reference returned is DECREF’d or stored in a structure that is later released. Fix any missing DECREF.
2. **test_formals.c:206 (test_formals_create_full):** Same: audit PyDict_SetItemString and similar; ensure keys/values are correctly owned and DECREF’d.
3. **formals.c:1438 and 1441 (koskari_formals_init):** Ensure dict keys/values used in PyDict_SetItemString are either borrowed or DECREF’d. If the dict is stored on the module and released at finalization, the leak may be in Python; otherwise add explicit cleanup in formals module clear.

### D. decorate_exception_with_dynchain (eval.c:380) — indirectly lost dict

1. **Context:** We set `__koskari_dynchain__` on the exception with `PyObject_SetAttrString(exc_val, "__koskari_dynchain__", frames)`. We DECREF `frames` after the call; the exception object `exc_val` is owned by the caller/top-level handler.
2. **Hypothesis:** The exception object (or an internal dict created by SetAttrString) is never DECREF’d when the exception is discarded in the eval path, so the 64-byte block (dict or similar) is indirectly lost.
3. **Fix:** Trace who owns `exc_val` after `decorate_exception_with_dynchain` and where it is DECREF’d. Ensure that when we leave the exception handler or replace the exception, the old exception (and thus the dynchain attribute) is DECREF’d. If the attribute is set on a temporary that is not freed, add a DECREF or clear the attribute before discarding.

### E. KosFormalsSpec_New (formals.c:231) — indirectly lost in test_eval_build_function_no_defaults

1. **Context:** test_eval.c:1272 builds a function and creates a FormalsSpec via `KosFormalsSpec_New`. The 64-byte block is the FormalsSpec object (or something it owns) that is not freed.
2. **Fix:** In the test, ensure the FormalsSpec (or the function/code that holds it) is DECREF’d before the test returns. In formals.c, ensure `KosFormalsSpec` type has correct tp_clear/tp_dealloc and that all owned references are cleared/decref’d so that when the test releases the function/code, the FormalsSpec is freed.

### F. Python init “definitely lost” (20,103 bytes in 410 blocks, etc.)

1. **Option (recommended for CI):** Add **Valgrind suppressions** in `valgrind-python.supp` for “definitely lost” whose stack is entirely inside `Py_InitializeFromConfig` / `Py_InitializeEx` / `initialize_python` (and similar init-only frames). This is standard for embedding Python: many init allocations are intentionally not freed. Match leak kind `definitely lost` and the init stack so we don’t hide our own leaks.
2. **Option:** Leave as-is and accept that Valgrind will report these until Python’s embed documentation or implementation changes. Focus on bringing our own leaks (formals, sync, eval dynchain) to zero.

---

## 5. How this report was produced

- Source: `valgrind.results` (single run of `/build/test_harness` under Memcheck).
- Leak kinds: definitely lost, indirectly lost, possibly lost.
- Stacks were grouped by the first frame in project code (koskari, tests, test_harness). Counts and sizes are from the LEAK SUMMARY and from scanning loss records for recurring call stacks (bytecode.c, eval.c, sync.c, formals.c, test_formals.c, test_harness.c, Py_Initialize*).

Re-run after applying fixes with:

```bash
make test-valgrind
```

and compare the new LEAK SUMMARY and ERROR SUMMARY to this report.
