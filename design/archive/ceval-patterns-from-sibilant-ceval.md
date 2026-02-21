# Good patterns from sibilant-ceval (for koskari/sibilant-ng)

Summary of patterns in sibilant-ceval code and tests, read with attention to detail, for reuse or adaptation in sibilant-ng.

---

## 1. Test layout and opcode tests

**Location:** `tests/lib/test_ceval.py`

- **Single-opcode focus:** Each test targets one opcode (or one opcode + one error path). The test builds a minimal bytecode program that runs that opcode, then asserts on stack/namespace/exception.
- **Explicit docstring:** The file opens with a short explanation of the strategy: "validate each individual bytecode operation and its impact on the frame, including error conditions like stack underruns."
- **Code as data in the test:** Programs are lists of `[opname, arg]` (e.g. `["VAR", "pizza"]`, `["CONST", "Hello World"]`). A helper turns these into real bytecode so the test reads like the program.

---

## 2. build_expr / step_expr: harness for expression tests

**Location:** `tests/lib/test_ceval.py` and `tests/lib/test_expression_function.py` (top of file)

- **build_expr(code, nspec=None, glbls=None):**
  - Takes a list of `[op, arg]` (op is string like `"NOOP"`, `"VAR"`).
  - Wraps the program in `YIELD` / `POP` so the expression is a generator: run to first yield (inject state), then run to second yield (return frame).
  - Normalizes nspec: `None` → empty `NamespaceSpec(None, (), ())`; tuple/list → `NamespaceSpec(None, *nspec)`.
  - Normalizes glbls: `None` → `get_glbls()`; dict → `get_glbls(**dict)`.
  - Returns `Expression(glbls, bytecode)`.
- **step_expr(expr, stack=None, values=None):**
  - Starts the expression (generator), runs to first yield.
  - Injects optional initial `stack` and namespace `values` (frame.fr_namespace[key] = val).
  - Runs to second yield and returns the frame for assertions.

So: one shared harness, many tests that only pass `code` (+ optional stack/values/nspec/glbls) and assert on the returned frame or on raised exceptions.

---

## 3. Opcode representation in tests

- **Ceval tests:** Use string opcode names and map with `OpCode[f"OP_{op}"]`, e.g. `["VAR", "pizza"]` → `(OpCode.OP_VAR, "pizza")`. That keeps test code readable and independent of enum numeric values.
- **Bytecode tests:** Use the enum directly, e.g. `[OpCode.OP_VAR, "YUS"]`, when the point is to check constant pool / oparg layout.

So: string names in ceval tests (clarity); enum in bytecode/structure tests (exact representation).

---

## 4. Namespace and “bind into the namespace”

**Locations:** `tests/lib/test_namespace.py`, `tests/lib/test_ceval.py` (e.g. test_var, test_fast)

- **Namespace is always the real type:** Tests use `Namespace(...)` or `ModuleNamespace(...)`. No plain dict as “namespace” in the test API; the VM may support dict internally, but tests exercise the real Namespace contract.
- **Names come from spec:** For VAR/FAST, the namespace is created with the right names in `local_syms` (or closure). Then values are put in with `ns.store(name, value)` or, in the ceval harness, `step_expr(expr, values={name: value})` which does `frame.fr_namespace[key] = val`.
- **test_var:** Builds code `[["VAR", "pizza"]]`, uses a real `Namespace("inner", get_glbls(), (), ())`, then `step_expr(expr, values={"pizza": "yes, please"})` and asserts stack peek is `"yes, please"`. So: callable/value is bound in the namespace; bytecode only loads by name (VAR). Same idea for FAST with `("pizza",)` in local_syms and again `values={"pizza": "yes, please"}`.

So: “bind it into the namespace” means use NamespaceSpec with the name in the right slot, get a Namespace from it, store the value (or use the harness’s `values=`), and have bytecode load via VAR (or FAST).

---

## 5. Shared test helpers and global namespace

**Location:** `tests/__init__.py` (e.g. `make_callable`; imports from `koskari.lib` and `koskari.types`), `tests/lib/test_ceval.py`

- **get_glbls(**defns):** Returns a `ModuleNamespace("module", builtins)` with a small set of builtins (`+`, `-`, `<`, `>`, `==`, `APPEND`, `ITER`, etc.). Tests can override/extend with `get_glbls(**defns)` or pass a custom namespace.
- **build_fspec(defn) / build_func(module, defn, **kwds):** Turn a single dict (code, formals, base_nspec, etc.) into a FunctionSpec or callable. Keeps func tests declarative and consistent.

So: shared, realistic “global” namespace and a single way to build functions for tests.

---

## 6. Assertion helpers for expected exceptions

**Location:** `tests/lib/test_ceval.py`, class CevalTest

- Helpers like `assertRuntimeError(expr, *args, **kwds)` wrap `self.assertRaises(RuntimeError, step_expr, expr, *args, **kwds)`. Same for AttributeError, IndexError, TypeError.
- Tests then say `self.assertRuntimeError(expr)` or `self.assertRaises(NameError, step_expr, expr)` instead of repeating the step_expr + exception pattern.

So: one line per “this run must raise X” case; less noise and consistent style.

---

## 7. Testing both success and failure paths

**Typical shape in ceval tests:**

- First: assert that invalid preconditions raise (e.g. stack underrun, wrong type, missing name).
- Then: one or more `step_expr(expr, stack=..., values=...)` with valid inputs and assert on frame (stack length, peek, or namespace contents).

Example (test_var): assert NameError when the name is not in the namespace; then inject `values={"pizza": "yes, please"}`, run, assert stack has one item and peek is that value.

So: each opcode test tends to cover both “error when preconditions fail” and “correct state when they hold.”

---

## 8. Function-level tests: code + formals + namespace in one place

**Location:** `tests/lib/test_ceval.py`, `tests/lib/test_expression_function.py` (function/callable tests)

- Tests build a `defn` dict with keys like `"code"`, `"formals"`, `"base_nspec"`; code is a list of `[OpCode.OP_*, arg]`.
- `base_nspec` is the frame’s namespace spec (e.g. `(None, (), ("FN", "VAL"))` for closure names).
- Callables (e.g. Python functions) are put in the **module** (glbls) and referenced by name in code (VAR "FN", VAR "VAL", …). No callable in CONST.
- `build_func(get_glbls(), defn)` or `build_func(ModuleNamespace("module", {...}), defn)` produces the callable; tests then call it and assert return value or side effects.

So: “bind callables in the namespace (or globals), reference by name in bytecode” is the consistent pattern; no putting callables in the constant pool.

---

## 9. Bytecode tests: constants and oparg

**Location:** `tests/compile/test_bytecode_fragment.py` (ByteCodeFragment/Code API), `tests/compile/test_bytecode.py` (#bytecode special form)

- **example_bytecode_truthy_yus():** Returns a ByteCode built from a tuple of instructions; used by both bytecode tests and (via import) func tests. So one canonical example, reused.
- Tests assert on `bc.oparg(i)`, `bc.constant(i)`, `bc.constants_pool`, `bc.length()` so that constant pool deduplication and instruction layout are checked explicitly.

So: bytecode tests focus on structure (oparg, constant pool, length); ceval tests focus on execution (stack, namespace, exceptions).

---

## 10. Module and namespace types

**Location:** `tests/lib/test_namespace.py`

- Tests distinguish `Namespace` (child of another namespace, fixed local_syms/closure_syms) and `ModuleNamespace` (top-level, can create refs on first access). They test store/deref/ref/clear, nesting, and closure semantics.
- No test uses a raw dict as the “namespace” object passed into the API; they use the real types and their methods (e.g. `ns.store('a', 123)`, `ns['a']`, `ns.deref('a')`).

So: tests that care about namespaces use the actual Namespace types and their contracts, not ad-hoc dicts.

---

## Summary for sibilant-ng (koskari)

1. **Ceval-style tests:** Consider a small harness (build code from list of [op, arg], run once, return result or frame) and one test per opcode or per behavior, with both error and success paths.
2. **Namespaces:** Use NamespaceSpec + get_namespace() + ns.store(name, value) for “bind into the namespace”; have bytecode load by name (VAR). Do not put callables (or other bindable values) in CONST for that purpose.
3. **Opcode in tests:** Prefer string names mapped to enum in execution tests (readability); use enum directly when testing bytecode structure.
4. **Shared helpers:** Centralize “empty ns”, “ns with these names”, “run this code and return result/frame” and reuse so tests stay short and consistent.
5. **Exception helpers:** Small assertRaises wrappers (assertNameError, assertRuntimeError, etc.) keep tests readable.
6. **One source of truth for examples:** A single small bytecode example (e.g. “truthy yus”) used by both bytecode and execution tests avoids drift.

These patterns from sibilant-ceval can guide how we structure koskari ceval tests and how we treat namespaces and constants.
