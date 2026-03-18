# Valgrind Memory Leak Report — Native Tests

This report identifies all **unsuppressed** leaks in the current `valgrind.results` (after `initialize_python` and `initialize_types` stack filters). Source: single run of `/build/test_harness` under Memcheck.

**Last updated:** After adding initialize_types suppression; 33 errors remain.

---

## Summary

| Category            | Bytes | Blocks |
|---------------------|-------|--------|
| **Definitely lost** | 1,375 | 23     |
| **Indirectly lost** | 128   | 2      |
| **Possibly lost**   | 585   | 10     |
| **Suppressed**      | 148,517 | 2,501 |
| **ERROR SUMMARY**   | 33 errors (903 suppressed) | |

---

## 1. Formals tests

### 1.1 test_formals_apply_with_defaults (test_formals.c:377) — 32 bytes, definitely lost

- **Stack:** libpython → `test_formals_apply_with_defaults` (test_formals.c:377) → run_formals_tests → main.
- **Context:** Test builds `pos`, `kwd`, `defaults` (with `PyDict_SetItemString(defaults, "b", PyLong_FromLong(999))`), `local_syms`, `formals`, `ns`, `args`, then applies formals. Line 377 is in the setup (e.g. `Py_BuildValue("(s)", "a")` or similar).
- **Cause:** A created object (likely from `Py_BuildValue` or another helper) is not DECREF'd or is not transferred to formals/ns and is never released.
- **Fix:** Audit every new reference created in the test (pos, kwd, defaults, the long 999, local_syms, formals, ns, args, and any temporaries). Ensure each is either DECREF'd or transferred to an object that is later DECREF'd. Pay special attention to `PyLong_FromLong(999)` passed to `PyDict_SetItemString` — the dict takes a new ref, so the caller must not hold a second ref unless it DECREFs it.

### 1.2 test_formals_create_full (test_formals.c:206) — 44 bytes, definitely lost

- **Stack:** `PyUnicode_New` → `PyDict_SetItemString` → `test_formals_create_full` (test_formals.c:206) → run_formals_tests → main.
- **Context:** Line 206 is `PyDict_SetItemString(defaults, "bar", PyLong_FromLong(42))`. The long is inserted into `defaults`; the dict holds one ref. The caller did not create a separate ref to the long, so no missing DECREF on the long. The 44-byte block is a **unicode** (PyUnicode_New), so the leak is likely a **string** used as a key or stored elsewhere — e.g. a key or value created by the test or by `KosFormals_NewFull` that is never released.
- **Cause:** Either a unicode key/value in the formals or defaults structure is not owned by any object that gets DECREF'd, or the test holds a ref it never DECREFs (e.g. a name or key).
- **Fix:** Check that every `PyUnicode_FromString` / string-returning API in this test is either DECREF'd or transferred to formals/defaults/pos/kwd/star; ensure formals and all its owned objects are released by the existing DECREFs (formals, defaults, star, kwd, pos). If `KosFormals_NewFull` does not take ownership of all passed-in references, the test must DECREF any it still owns.

---

## 2. Namespace tests

### 2.1 test_namespace_nested (test_namespace.c:303, 304, 305) — 44 bytes × 3, definitely lost

- **Stack:** `PyUnicode_New` → … → `test_namespace_nested` (lines 303, 304, 305) → run_namespace_tests → main.
- **Context:** Lines 301–305: `name1 = PyUnicode_FromString("parent")`, `name2 = PyUnicode_FromString("child")`, `local1 = PyTuple_Pack(1, PyUnicode_FromString("foo"))`, `local2 = PyTuple_Pack(1, PyUnicode_FromString("bar"))`, `closure2 = PyTuple_Pack(1, PyUnicode_FromString("foo"))`. So three unicode allocations at 303–305 are the inner `PyUnicode_FromString` calls used in `PyTuple_Pack`. `PyTuple_Pack` takes new references and INCREFs the items, so the caller must DECREF the strings passed to Pack (the tuple holds its own ref). If the test never DECREFs those strings, three refs leak — matching three 44-byte blocks.
- **Cause:** The strings passed to `PyTuple_Pack` (e.g. for "foo", "bar", "foo") are not DECREF'd by the test after the tuple is created. The tuple holds a ref to each; the caller's ref is still held.
- **Fix:** After creating local1, local2, closure2, DECREF the strings that were passed to PyTuple_Pack (or create the strings into temporaries and DECREF them immediately after each Pack call). Alternatively use a pattern that doesn't leave a second ref (e.g. build the tuple in a way that transfers ownership).

### 2.2 test_collector_blocks_closure (test_namespace.c:458, 459, 460) — 44 bytes × 3, definitely lost

- **Stack:** `PyUnicode_New` → … → `test_collector_blocks_closure` (lines 458, 459, 460) → run_namespace_tests → main.
- **Context:** Same pattern as above: unicode strings created (likely for closure/block names or keys) and not DECREF'd.
- **Fix:** Same as §2.1 — ensure every string created for Pack or dict/namespace use is DECREF'd or transferred.

### 2.3 test_namespace_nested_deep (test_namespace.c:755) — 45 bytes, definitely lost

- **Stack:** `PyUnicode_New` → … → `test_namespace_nested_deep` (test_namespace.c:755) → run_namespace_tests → main.
- **Context:** One unicode allocation at line 755 not released.
- **Fix:** Find the PyUnicode_FromString or similar at 755 and ensure the returned ref is DECREF'd or transferred.

---

## 3. Eval tests

### 3.1 test_eval_push_cc_capture_cc_pop_cc_returns_continuation (test_eval.c:487) — 47 bytes, definitely lost

- **Stack:** `PyUnicode_New` → … → `test_eval_push_cc_capture_cc_pop_cc_returns_continuation` (test_eval.c:487) → run_eval_tests → main.
- **Context:** A unicode (e.g. a name or key) created around line 487 is not DECREF'd.
- **Fix:** Audit line 487 and nearby; DECREF or transfer every new unicode ref.

### 3.2 test_eval_call_zero_args (test_eval.c:1189) — 47 bytes, definitely lost

- **Stack:** `PyUnicode_New` → … → `test_eval_call_zero_args` (test_eval.c:1189) → run_eval_tests → main.
- **Context:** Same pattern: unicode at 1189 not released.
- **Fix:** Same as §3.1.

### 3.3 test_eval_call_continuation_underrun — 64 bytes, indirectly lost (decorate_exception_with_dynchain)

- **Stack:** `PyDict_New` → `PyObject_SetAttrString` → `decorate_exception_with_dynchain` (eval.c:380) → `inject_eval_frame_traceback` → … → `test_eval_call_continuation_underrun` (test_eval.c:581) → main.
- **Context:** When we set `__koskari_dynchain__` on the exception with `PyObject_SetAttrString`, Python may allocate a dict for the exception's attributes. The exception value is returned in the event and should be DECREF'd by the test (we added Py_XDECREF(ev.exc_type) and ev.payload). If the payload is DECREF'd, the exception's __dict__ should be freed. So either the exception is still not fully released (e.g. one more ref somewhere), or the type was not DECREF'd in some path.
- **Cause:** Previously attributed to missing exc_type DECREF; if still present, check that both ev.payload and ev.exc_type are DECREF'd on every path in this test and that no other code holds the exception.

### 3.4 test_eval_build_function_no_defaults — FormalsSpec + FunctionSpec (indirectly + definitely)

- **64 bytes indirectly lost:** `_PyObject_New` → `KosFormalsSpec_New` (formals.c:231) → `test_eval_build_function_no_defaults` (test_eval.c:1290) → main. The FormalsSpec is created and passed to KosFunctionSpec_New; the test DECREFs formals_spec and inner_code. The fspec is put in the code's const pool. When code_obj is DECREF'd, the const pool should be cleared and the fspec DECREF'd, which should free the FormalsSpec. So the FormalsSpec leak suggests the code object's const pool is not releasing the fspec, or the fspec is not releasing the formals_spec (ownership chain bug).
- **104 bytes (40 direct + 64 indirect) definitely lost:** `PyType_GenericNew` → `KosFunctionSpec_New` (func.c:425) → `test_eval_build_function_no_defaults` (test_eval.c:1294) → main. The FunctionSpec object itself is not freed; the 64 indirect is the FormalsSpec it owns. So the **root** leak is the FunctionSpec (and thus its formals_spec): something in the test or in the code object's lifecycle is not releasing the fspec stored in the instruction/const pool.
- **Fix:** In the test, the fspec is stored in the first instruction via `make_instr(KOS_OP_CONST, fspec)`. The instruction list is given to KosCode_New; the code builds a const pool from instructions. Ensure that when the code object is DECREF'd, every const (including the fspec) is DECREF'd in code_clear/code_dealloc. If the bytecode implementation does not include the fspec in the const pool or does not DECREF it, fix the bytecode module. If the test never DECREFs the code_obj or the instruction list that holds the fspec, fix the test (the test does Py_DECREF(code_obj) and Py_DECREF(instr); instr is DECREF'd but the code may have taken ownership of the list's items — verify that the code's const pool holds the only refs to the fspec and that they are released).

### 3.5 test_eval_call_zero_args (test_eval.c:1208) — 104 bytes, definitely lost (dynframe clone)

- **Stack:** `kos_dynnode_alloc` → `kos_dynframe_clone` → … → `run_transfer_plan_to_completion` (eval.c:122) → `koskari_eval` → `test_eval_call_zero_args` (test_eval.c:1208) → main.
- **Context:** During eval, a frame is cloned (e.g. for transfer/continuation). The cloned frame node is allocated but not freed by the time the test exits.
- **Cause:** Either the transfer plan or the eval loop does not free cloned frames when the plan completes or when the context is freed; or the test does not free the context in a way that tears down the plan and its nodes.
- **Fix:** Ensure kos_context_free (or the path that runs when eval returns) frees all nodes created during the run (including cloned frames in the transfer plan). Audit run_transfer_plan_to_completion and the plan teardown.

### 3.6 test_eval_reset_shift_invoke (test_eval.c:442) — 168 bytes (104 direct + 64 indirect), definitely lost

- **Stack:** `kos_dynnode_alloc` → `kos_dynframe_clone` → … → `xfer_step_push` / `kos_run_transfer_step` → `run_transfer_plan_to_completion` → `koskari_eval` → `test_eval_reset_shift_invoke` (test_eval.c:442) → main.
- **Context:** Same as §3.5: a cloned frame (and possibly an associated 64-byte block) is not freed.
- **Fix:** Same as §3.5 — ensure transfer plan and cloned nodes are freed when the context is freed or when the plan completes.

---

## 4. Bytecode tests

### 4.1 test_code_create_minimal_return_none (test_bytecode.c:125) — 56 bytes, definitely lost

- **Stack:** `PyTuple_New` → `make_instr` (test_bytecode.c:59) → `test_code_create_minimal_return_none` (test_bytecode.c:125) → main.
- **Context:** `make_instr` allocates a 2-tuple with `PyTuple_New(2)` and sets items. The tuple is returned and presumably put into an instruction list and then into KosCode_New. If the code takes ownership of the list and its items, the tuple should be released when the code is DECREF'd. If the test builds an instr list, passes it to KosCode_New, then DECREFs the list but the code did not INCREF the items, the tuple could be freed by the list's dealloc and there would be no leak — unless the code holds a ref and never releases it. So the leak is either: (1) the test never DECREFs the instruction list or the code, or (2) the code holds a ref to the tuple (e.g. in const pool) and does not DECREF it in code_clear/code_dealloc. Given the stack points at make_instr and test_code_create_minimal_return_none, the tuple is created for an instruction; if that instruction is stored in the code and the code is not freeing it, the leak is in the code module. If the test does not pass the instr to the code or does not DECREF the code, the leak is in the test.
- **Fix:** Ensure the test DECREFs the code object and the instruction list. Ensure KosCode_New/code_instr_init and code_clear/code_dealloc correctly own and release instruction objects (tuples) and const pool entries.

### 4.2 test_code_position_delta (test_bytecode.c:222, 224, 229, 232, 234) — 56 bytes × 5, definitely lost

- **Stack:** `PyTuple_New` → `make_position` (test_bytecode.c:78) → `test_code_position_delta` (lines 222, 224, 229, 232, 234) → main.
- **Context:** `make_position(line, col)` allocates a tuple with `PyTuple_New(2)` and sets items. Five such tuples are created and not released. They are likely passed into a position list or code object; if the code/position list takes ownership, they should be freed when the code is freed. So either the test does not DECREF the code/position structure, or the code does not release these tuples.
- **Fix:** Same as §4.1 — verify test DECREFs and that the code/position handling in bytecode releases all stored tuples.

---

## 5. Dynchain tests (native allocator)

### 5.1 test_dynframe_new_free (test_dynchain.c:40) — 104 bytes, definitely lost

- **Stack:** `kos_dynnode_alloc` (dynchain.c:122) → `kos_dynframe_new` (dynchain.c:162) → `test_dynframe_new_free` (test_dynchain.c:40) → main.
- **Context:** Test creates a frame with `kos_dynframe_new(NULL, NULL, ns, NULL)` then calls `kos_dynframe_free(frame)`. The 104-byte block is the node allocated by `kos_dynnode_alloc` inside `kos_dynframe_new`.
- **Cause:** `kos_dynframe_free` likely does not call the allocator's free (or equivalent) for the frame node. So the node allocated in kos_dynframe_new is never freed.
- **Fix:** In dynchain.c, ensure `kos_dynframe_free` (or the generic node free path) frees the memory returned by `kos_dynnode_alloc` for the frame. Match alloc/free (e.g. if allocation is via PyObject_GC_New or a custom allocator, use the corresponding free).

### 5.2 test_chain_free (test_dynchain.c:236) — 104 bytes, definitely lost

- **Stack:** `kos_dynnode_alloc` → `kos_dynprompt_new` (dynchain.c:365) → `test_chain_free` (test_dynchain.c:236) → main.
- **Context:** Test builds a chain of prompts (p1, p2, p3) and calls `kos_dynchain_free((KosDynNode *) p3, NULL)` to free the chain.
- **Cause:** Either `kos_dynchain_free` does not free all nodes (e.g. only frees from the given node down to a stop node and NULL is interpreted in a way that leaves nodes alive), or it does not actually call free on each node. One 104-byte block (one prompt node) remains.
- **Fix:** Verify `kos_dynchain_free` walks from the given node to the root (parent == NULL) and frees every node. Ensure each node's memory is freed (same allocator as in §5.1).

### 5.3 test_chain_clone_segment (test_dynchain.c:260) — 104 bytes, definitely lost

- **Stack:** `kos_dynnode_alloc` → `kos_dynprompt_clone` → … → `kos_clone_chain_segment` → `test_chain_clone_segment` (test_dynchain.c:260) → main.
- **Context:** Test clones a segment; the cloned chain is returned. The test likely verifies the clone and then must free it. If the test does not free the cloned segment, the clone's nodes leak.
- **Cause:** The cloned segment (or one of its nodes) is never freed. Either the test does not call a free function for the cloned segment, or the free function does not free cloned nodes.
- **Fix:** After verifying the clone, call the appropriate free (e.g. kos_dynchain_free) on the cloned segment. Ensure the free path correctly frees cloned nodes.

---

## 6. Possibly lost (585 bytes, 10 blocks)

Valgrind reports "possibly lost" when pointers might be interior to a block. These are often false positives. No distinct stacks were listed in the snippet; they are lower priority. Re-run with `--show-leak-kinds=all` and inspect each "possibly lost" record if you need to confirm whether any are real leaks in our code.

---

## 7. Recommended fix order

1. **Dynchain alloc/free (high):** Fix §5.1–5.3 so that `kos_dynframe_free` and `kos_dynchain_free` (and any path that frees cloned segments) actually free the memory from `kos_dynnode_alloc`. This removes 416 bytes (4 blocks) and likely prevents the eval leaks in §3.5–3.6 if those nodes are the same kind.
2. **Bytecode test / code object (high):** Fix §4.1–4.2 so that instruction/position tuples are released (either by the test DECREF'ing the code and ensuring the code's const pool is cleared, or by fixing code_clear/code_dealloc to release all stored tuples). This removes 336 bytes (6 blocks).
3. **Formals/namespace/eval unicode (medium):** Fix §1.1–1.2, §2.1–2.3, §3.1–3.2 by auditing refcounts for every PyUnicode_FromString, Py_BuildValue, and PyDict_SetItemString (ensure callers DECREF or transfer new refs). This removes the remaining small definitely-lost and clarifies §3.3–3.4.
4. **Eval build_function + dynchain (medium):** Fix §3.4 (FunctionSpec/FormalsSpec not released from code const pool or test) and §3.5–3.6 (transfer plan / cloned frames freed in context teardown). This removes the 64+104+104+168 bytes from eval.

Re-run after changes:

```bash
make test-valgrind
```

and compare the new LEAK SUMMARY to this report.
