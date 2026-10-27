# compile

**Symbol:** `compile`

**Syntax:** `(compile source ns)`

**Use-case:** Compile a Koskari source object to an invokable expression without
running it. Useful when you want to inspect or reuse compiled code separately
from evaluation.

**Behaviour:** If `source` is already an **`Expression`** or **`Function`**, it is
returned unchanged. Otherwise the module's compiler, obtained through
[get-compiler](get-compiler.md), compiles the source as a top-level expression
with **`compile_expression`**; the fragment is converted with
**`as_expression`** and returned.

**Relation to the macro:** **`(compileq form)`** and **`(compileq form ns)`** are
handled by the **`compile/compileq`** macro, which quotes the form and expands
to **`(call compile ...)`**. See [compile/compileq](../macros/compile-compileq.md).

**Examples:**

```koskari
(call compile '(lambda [x] (+ x 1)) (vars))
```

See also [evaluate](evaluate.md) to compile and run in one step.
