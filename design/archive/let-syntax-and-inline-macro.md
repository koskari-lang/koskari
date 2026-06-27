# let-syntax and inline-macro

**Status:** Implemented (archived). Shipped as bootstrap compile specials
**`compile/let-syntax`** (surface **`let-syntax`**) and **`compile/inline-macro`**
(surface **`inline-macro`**).

**Canonical summary:** [MACROS.md](../MACROS.md) § compile/let-syntax and
§ compile/inline-macro.

**User documentation:**

- [docs/reference/macros/compile-let-syntax.md](../../docs/reference/macros/compile-let-syntax.md)
- [docs/reference/macros/compile-inline-macro.md](../../docs/reference/macros/compile-inline-macro.md)

**Tests:** `tests/importer/test_let_syntax.py`

---

## As-built

### let-syntax

**`compile/let-syntax`** is a **`special-rules`** compile special in
**`koskari/bootstrap/_define_syntax.ki`**. At expand time it builds a
**`(syntax-rules NAME literals rule …)`** form, compiles and evaluates it to
obtain a **`Syntax`** transformer, then compiles the body inside
**`Compiler.macro_overlay`**. The overlay stack is consulted by
**`find_compiled`** before the module namespace. Nested **`let-syntax`** shadows
outer bindings. Nothing is stored in the module namespace.

Current bootstrap arm accepts **`syntax-rules`** only (unary binding).

### inline-macro

**`compile/inline-macro`** is a **`special-rules`** compile special in
**`koskari/bootstrap/basics.ki`**. At expand time the body forms are unwrapped
from **`Datum`**, combined into **`(begin body …)`**, compiled and evaluated
once via **`full_compile`** and **`as_expression`**. The resulting form is
returned for the compiler to compile in place of the **`inline-macro`** site
(compile-time source splice). No transformer is installed.

---

## Historical draft (superseded)

The sections below record an early design that proposed a **`compile/macro-let`**
compiler special and an **`inline-macro`** desugaring into **`macro-let`**. That
surface was not implemented. Local compile-time macro binding shipped as
**`let-syntax`** using the existing **`macro_overlay`** API on **`Compiler`**
(**`macro_overlay`** / **`macro_overlays`** context managers in
**`koskari/compile/compiler.py`**).

---

## Original purpose (draft)

Install temporary compile-time macros for the dynamic extent of a body without
storing them in the module namespace. Two spellings were contemplated:

- Multi-binding local macros (draft name **`macro-let`**)
- Single anonymous splice ( **`inline-macro`** )

## Original compiler model (draft)

**`Compiler`** overlay stack; **`find_compiled`** consults overlay before module
namespace. A dedicated **`compile/macro-let`** special was planned in
**`koskari/compile/specials/macro_let.py`**.

## Original inline-macro desugaring (draft)

```koskari
(define-syntax compile/inline-macro:
  syntax-rules []:
    case (inline-macro body ...):
      (let [[_name (gensym 'inline-macro)]]
        (macro-let [[_name [] body ...]]
          (_name))))
```

**As-built:** **`inline-macro`** evaluates and splices source directly (see
**`basics.ki`**); it does not desugar through a local macro binding.

## Related

- [../SPECIALS.md](../SPECIALS.md) — compile/syntax-rules, compile/macro-match
- [labels-and-flet.md](../language/labels-and-flet.md) — independent bootstrap
  macros (still open)

# The end.
