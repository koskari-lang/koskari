# evaluate

**Symbol:** `evaluate`

**Syntax:** `(evaluate source ns)`

**Use-case:** Compile a Koskari form (if needed) and run it in a namespace, returning
the result. This is the programmatic equivalent of typing a form at the REPL:
read a source object, compile it with the namespace's compiler, invoke the
resulting expression, and return its value.

**Behaviour:** If `source` is already compiled **`Code`**, its **`expression`**
method turns it into an invokable expression in `ns`. Otherwise **`compile`** is
called with `source` and `ns`, which returns an invokable expression (or
`source` itself when it is already an **`Expression`** or **`Function`**). The
expression is called, and its return value is the value of the **`evaluate`**
call.

The namespace must provide a **`__compiler__`** binding when compilation is
required (the REPL and loaded modules set this up automatically).

**Relation to the macro:** **`(evaluate form)`** and **`(evaluate form ns)`**
are handled by the **`compile/evaluate`** macro, which expands to **`(call
evaluate …)`** with **`(locals)`** when the namespace argument is omitted. See
[compile/evaluate](../macros/compile-evaluate.md).

**Examples:**

```koskari
;; One-argument form uses (locals) via the compile/evaluate macro
(evaluate (+ 1 2))

;; Explicit namespace
(evaluate (+ x y) my-ns)

;; Direct call when the namespace is already in hand
(call evaluate '(+ 1 2) (locals))
```
