# macroexpand

**Symbol:** `macroexpand`

**Syntax:** `(macroexpand form ns)`

**Use-case:** Expand a source form repeatedly until its head is no longer a macro.
This is what the compiler does before compiling the final form. Use
[macroexpand-1](macroexpand-1.md) when you need to inspect intermediate steps.

**Behaviour:** Like **`macroexpand-1`**, but loops: after each successful expansion,
the result becomes the new form and expansion is attempted again until the head
is not a macro, no transformer matches, or the binding is not **`Syntax`**.

Single-expression **`begin`** wrappers are stripped after each step, same as
**`macroexpand-1`**.

The namespace must provide **`__compiler__`**.

**Relation to the macro:** **`(macroexpand form)`** and **`(macroexpand form ns)`**
are handled by the **`compile/macroexpand`** macro. The **`macroexpandq`** and
**`macroexpandq-1`** macros quote the form before calling. See
[compile/macroexpand](../macros/compile-macroexpand.md) and
[compile/macroexpandq](../macros/compile-macroexpandq.md).

**Examples:**

```scheme
;; Fully expand a form in the globals namespace
(macroexpand (define x 1))

;; Expand from surface syntax
(macroexpandq (define x 1))

;; Direct call with an explicit namespace
(call macroexpand when-form (globals))
```

See also [macroexpand-1](macroexpand-1.md) for a single step.
