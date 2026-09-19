# get-compiler

**Symbol:** `get-compiler`

**Syntax:** `(get-compiler ns)`

**Use-case:** Obtain the compiler for the module that owns a namespace, so that
source can be compiled or macro-expanded at run time. **`compile`**,
**`evaluate`**, **`macroexpand`**, and **`macroexpand-1`** all take their
compiler this way.

**Behaviour:** Walks from `ns` up through enclosing function and `let`
namespaces to the module namespace. If the module has a **`__compiler__`**
binding, that compiler is returned. Otherwise a new compiler is built from the
module's **`__pragma__`** (or from the nearest package pragma, or a default
pragma, when no pragma is bound), bound as **`__compiler__`**, and returned. A
later call finds the bound compiler.

A module keeps **`__pragma__`** after loading but drops **`__compiler__`** and
**`__reader__`**, so the first run-time compile in a module rebuilds its
compiler and later ones reuse it.

**Relation to the macro:** **`(get-compiler)`** and **`(get-compiler ns)`** are
handled by the **`compile/get-compiler`** macro, which expands to **`(call
get-compiler ...)`** with **`(locals)`** when the namespace argument is omitted.
See [compile/get-compiler](../macros/compile-get-compiler.md).

**Examples:**

```koskari
;; One-argument form uses (locals) via the compile/get-compiler macro
(define c (get-compiler))

;; Compile a form by hand with the module compiler
(define frag (.compile_expression (get-compiler) '(+ 1 2)))
```

See also [compile](compile.md) and [evaluate](evaluate.md).
