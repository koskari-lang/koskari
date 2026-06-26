# macro-let and inline-macro

## Status

Proposed — evaluate and implement.

## Purpose

**`macro-let`** installs temporary compile-time macros for the dynamic extent of
its body. The body is **compiled** under those macros without **`macro-let`**
expanding into an outer form first. That makes it possible to:

- Define ad-hoc expanders visible only inside a function or module fragment.
- Write macro logic that must see the compiler context for the surrounding
  forms.
- Use **`inline-macro`** as sugar for a single anonymous macro around a block
  of body forms.

**`inline-macro`** is a bootstrap macro only; **`macro-let`** is a compiler
special.

## Why a special

Compile-time macros normally expand to another form, then compilation
continues. **`macro-let`** instead overlays the compiler’s name resolution,
builds **`Syntax`** objects for each local macro, and compiles the body in that
overlay. A macro cannot implement that (it would always produce an expanded
surrogate). **`macro-let`** therefore registers as **`compile/macro-let`**.

## Surface syntax

```koskari
(macro-let [[twice [x] `(+ ,x ,x)]]
  (twice 5))

(inline-macro
  (define-syntax helper ...)
  (helper form))
```

**Declarations:** **`[[name formals body ...] ...]`** — same shape as a
**`define-syntax`** procedural macro: name, formal parameters matching the
call pattern, body returning expanded source.

**Body:** one or more forms; compiled as **`begin`** under the overlay.

Surface alias **`macro-let`** → **`compile/macro-let`** via **`define-syntax`**
in bootstrap (same pattern as **`if`** / **`compile/if`**).

## Compiler model

### Overlay stack

**`Compiler`** gains a stack of overlay dicts (name → **`Syntax`** / **`Compiled`**).
**`find_compiled`** consults the topmost overlay before the module namespace.
Nested **`macro-let`** pushes and pops frames on a context manager
(**`macro_let_scope`**).

### Building local macros

For each declaration **`(name formals . body)`**:

1. Derive a matcher pattern **`(name . formals)`** from the formals list.
2. Compile a handler from the body (procedural macro: invoke body with
   captures, run hygiene via **`_finalize_macro_output`**).
3. Wrap in **`Syntax(name, (MacroTransform(pattern, handler),))`** via
   **`build-macro`** or an internal helper shared with **`define-syntax`**.

Store in the overlay under **`name`**; **`find_compiled`** resolves bare
**`name`** and **`compile/name`** per existing rules.

### Special handler

**`koskari/compile/specials/macro_let.py`**:

1. Parse declarations and body.
2. Build overlay mapping.
3. **`with compiler.macro_let_scope(overlay):`** compile body as **`begin`**.
4. Return merged **`ByteCodeFragment`**.

Register in **`koskari/compile/specials/__init__.py`**.

## inline-macro

Bootstrap macro in **`koskari/bootstrap/_functions.ki`** (or a dedicated
**`_macros.ki`** if the file grows):

```koskari
(define-syntax compile/inline-macro:
  syntax-rules []:
    case (inline-macro body ...):
      (let [[_name (gensym 'inline-macro)]]
        (macro-let [[_name [] body ...]]
          (_name))))
```

**`gensym`** avoids colliding with user names. No **`with-gensyms`** helper
required for v1.

## Tests

**`tests/compile/specials/test_macro_let.py`** (`phase_specials`):

| Case | Assert |
| ---- | ------ |
| Simple local macro | Expansion visible only inside body |
| Outer scope | Global macro unchanged outside **`macro-let`** |
| Nesting | Inner overlay shadows outer for same name |
| **`inline-macro`** | Desugars and runs |
| Hygiene | Macro-introduced bindings do not capture user symbols |

## User documentation

- **`docs/reference/specials/compile-macro-let.md`**
- **`docs/reference/macros/compile-inline-macro.md`**
- **`design/SPECIALS.md`** — **`compile/macro-let`** entry
- **`design/MACROS.md`** — **`inline-macro`**
- Optional corpus records for expand-only behaviour

## Implementation order

1. Short design note alignment (this document).
2. **`Compiler.macro_let_scope`** and **`find_compiled`** overlay.
3. **`compile/macro-let`** special.
4. **`inline-macro`** bootstrap macro and **`macro-let`** alias.
5. Specials-phase tests.
6. Reference docs.

## Related

- **`koskari/syntax.py`** — **`Syntax`**, **`MacroTransform`**, **`build-macro`**.
- **`koskari/compile/compiler.py`** — **`find_compiled`**, **`compile_special`**.
- [../SPECIALS.md](../SPECIALS.md) — **`compile/syntax-rules`**, **`compile/macro-match`**.
- [labels-and-flet.md](labels-and-flet.md) — bootstrap macros in the same harvest
  batch (independent implementation).
