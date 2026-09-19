# compile/inline-macro

**Symbol:** `compile/inline-macro` (surface: **`inline-macro`**)

**Syntax:** **`(inline-macro body …)`**

**Behaviour:** **`special-rules`** compile special. At expand time the body
forms are unwrapped from **`Datum`**, combined into **`(begin body …)`**,
compiled and evaluated once via **`full_compile`** and **`as_expression`**. The
resulting form is returned for the compiler to compile in place of the
**`inline-macro`** site (compile-time source splice). A body that evaluates
to `None` splices the constant `None`, as does any other value with no form
of its own. No transformer is installed and nothing is bound in the module
namespace.

Use **`inline-macro`** when a block of forms must run at compile time and the
result replaces the site in the surrounding compile (for example to inject a
computed literal or configuration value).

## Example

```koskari
(inline-macro
  (quote (+ 10 32)))
;; => 42
```

The body is evaluated during compilation; the value **`42`** is what the
surrounding module compiles as the result of the **`inline-macro`** form.

## Related

- [compile/let-syntax](compile-let-syntax.md)
- [compile/define-syntax](compile-define-syntax.md)
- [Macro matching and hygiene](../../guide/macro-matching-and-hygiene.md)
