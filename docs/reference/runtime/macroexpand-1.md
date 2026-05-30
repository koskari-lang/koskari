# macroexpand-1

**Symbol:** `macroexpand-1`

**Syntax:** `(macroexpand-1 form ns)`

**Use-case:** Apply a single macro-expansion step to a source form. Useful for
debugging macros: see exactly one transformation before the compiler takes
over. For full expansion to a non-macro form, use [macroexpand](macroexpand.md).

**Behaviour:** Looks up **`__compiler__`** on **`ns`**, finds a compiled binding for
the form's head, and if it is a **`Syntax`** transformer, runs one
**`handle_expand`** pass. If the head is not a macro, or no transformer matches,
the form is returned unchanged.

When the expansion is a single-expression **`begin`** wrapper, that wrapper is
stripped so the result is the inner expression.

The form must already be a read source object (a pair or atom). To expand from
written surface syntax, use the **`macroexpandq-1`** macro or quote the form
yourself.

**Relation to the macro:** **`(macroexpand-1 form)`** and **`(macroexpand-1 form
ns)`** are handled by the **`compile/macroexpand-1`** macro, which expands to
**`(call macroexpand-1 …)`** with **`(globals)`** when the namespace argument is
omitted. See [compile/macroexpand-1](../macros/compile-macroexpand-1.md).

**Examples:**

```koskari
;; One expansion step using the current globals namespace
(macroexpand-1 (when condition action))

;; Explicit namespace
(macroexpand-1 some-form my-ns)

;; From surface syntax without pre-reading
(macroexpandq-1 (when condition action))
```
