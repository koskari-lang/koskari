# KOS_OP_BUILD_FUNCTION
- **Style**: EXTRA (`CONST`, `CONST`, `NOARG`, `NOARG`). Instruction arg = index into the code object's extra table; that row holds `a0` = constant pool index of the **keyword-defaults names tuple** and `a1` = constant pool index of the **annotation names tuple**. Either or both may be the empty tuple, meaning no values of that kind on the stack.
- **Stack**: `[..., ann_1, ..., ann_M, def_1, ..., def_N, fspec] → [..., function]`, where N = len(defaults names tuple) and M = len(annotation names tuple); either may be 0. The VM pops fspec, then (when N > 0) pops N values into a keyword-defaults dict keyed by the defaults names tuple, then (when M > 0) pops M more values into an annotations dict keyed by the annotation names tuple, and builds the function from the spec against the current frame namespace. The annotations dict becomes the function's creation-time annotations: values are real objects, except str values, which remain forward references resolved lazily on first `__annotations__` / `__signature__` access.
- **Invariants**: Both extra-row fields must be constant-pool tuples; the stack must hold M + N values below fspec, with annotation values below default values.
- **Compiler form:** Pre-compile extra operand is a 4-tuple `(defnames_tuple, annotation_names_tuple, 0, 0)`; `NOARG` slots are ignored by the assembler. The annotation names tuple is the annotated parameter names in order, followed by `"return"` when a return annotation is present.

**Example:** Build annotated function with one keyword default, as for `(lambda [(a :: int value: 9)] a)`. Pool: 0 = int, 1 = 9, 2 = function_spec; extra row: `(("a",), ("a",), 0, 0)`. Covered by `tests/lib/opcode/test_build_function.py`.

| index | opcode         | arg | arg view              | comment |
|------:|----------------|-----|-----------------------|---------|
| 0     | CONST          | 0   | 0 (int)               | annotation value; stack: [..., int] |
| 1     | CONST          | 1   | 1 (9)                 | default value; stack: [..., int, 9] |
| 2     | CONST          | 2   | 2 (fspec)             | stack: [..., int, 9, fspec] |
| 3     | BUILD_FUNCTION | 0   | 0 (("a",), ("a",), …) | stack: [..., function] |
| 4     | RETURN         | 1   | 1                     | |

Un-annotated, no-defaults form: extra row `((), (), 0, 0)`; only fspec is popped.
