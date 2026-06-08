# build-slice

Runtime builtin (not a macro): defined in bootstrap `_iter.ki`.

**Syntax:** `(build-slice)` or `(build-slice start: START stop: STOP step: STEP)`

**Behaviour:** Constructs a Python **`slice`** object. Keyword arguments default to
**`None`** when omitted. **`(build-slice)`** is equivalent to **`(slice None None None)`**.

The **`slice`** macro expands through this function so slice bounds can be passed
with keywords. See [compile/slice and compile/item-slice](../macros/compile-slice-and-compile-item-slice.md).
