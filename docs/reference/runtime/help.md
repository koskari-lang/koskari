# help

**Symbol:** `help`

**Syntax:** `(help name ns)`

**Use-case:** Display documentation for a Koskari binding in a namespace. Output
goes through the system pager (like Python's **`help()`**). Use at the REPL to
inspect macros, layout handlers, syntax transformers, and other registered
objects.

**Behaviour:** Collects help text for **`name`** by looking up bindings in
**`ns`**. For a simple name such as **`var`**, lookup tries **`var`**,
**`compile/var`**, **`layout/var`**, and **`match/var`**. Names that
already start with **`compile/`**, **`layout/`**, or **`match/`** are looked up
directly.

Registered help handlers format **`Syntax`**, layout handlers, and other types;
objects without a handler fall back to generic documentation when available.

**Relation to the macro:** **`(help)`**, **`(help obj)`**, and **`(help obj ns)`**
are handled by the **`compile/help`** macro. With no arguments, help is shown
for the name **`help`** itself. With one argument, **`obj`** is quoted and
**`(globals)`** is used. See [compile/help](../macros/compile-help.md).

**Examples:**

```koskari
;; Help for the help function itself
(help)

;; Help for a name in the globals namespace
(help var)
(help if)

;; Fully qualified names
(help compile/define)
(help layout/if)

;; Explicit namespace
(help compile/lambda my-ns)
```
