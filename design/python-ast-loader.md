# Koskari Design: Python AST Loader (Transformative Parser)

## Status

Proposal. No implementation yet.

## Summary

Enable loading Python modules as Koskari modules at import time by using a **special reader** that acts as a **transformative parser**: it consumes Python AST and produces equivalent Koskari expression trees (the same form-tree shape the compiler already expects). The existing Compiler and specials are unchanged; they consume the transformed forms exactly as they do for `.ki` source.

This allows Python syntax to be executed on the Koskari VM by transforming it into the same intermediate representation that `.ki` source produces after parsing.

## Motivation

- Run Python-style modules on the Koskari evaluator without requiring authors to write s-expr `.ki` source.
- Reuse the full Koskari compiler and special-form set; no second compiler or VM path.
- Unify the load path: one compile–execute loop, with the only variable being **how** top-level expressions are produced (LayoutReader for `.ki` vs. transformative parser for Python AST).

## Design Goals

- **Single pipeline**: The same `Compiler`, `SPECIALS`, and execution path (e.g. `load_module_demand`) drive both `.ki` and Python-origin modules. Only the “reader” that yields top-level forms changes.
- **No compiler/special changes**: Koskari specials remain keyed by symbol and accept `(compiler, source: pair, tc)`. They do not need a “Python mode”; they only see normal Koskari form trees.
- **Form tree as contract**: The transformative parser must output proper lists (pairs) built with `cons`, `symbol`, `nil` from `koskari.lib`, so that `is_pair` / `is_proper` and existing special-form destructuring work unchanged.

## Non-Goals

- Full Python semantics or compatibility; a useful subset is enough for v1.
- Changing the Koskari language or specials to be “Python-aware.”
- Interpreting Python bytecode; the input is Python **AST** (e.g. from `ast.parse`), not `.pyc`.

## Architectural Fit

Current module load flow:

1. **Reader** produces a stream of top-level **source expressions** (each a Koskari form tree: pairs, symbols, constants).
2. For each expression: `compiler.full_compile(source_expr)` → `ByteCodeFragment`; then execute the resulting expression in the module namespace (e.g. `demand_sync(expr)`).

Today the reader is **LayoutReader** over `.ki` text; it parses source into s-exprs. The proposal is to add an alternative reader that:

- Accepts a **Python AST** (e.g. `ast.Module` from `ast.parse(module_source)`).
- Walks the AST and, for each top-level statement (or logical grouping), **emits one Koskari form tree** that expresses the same semantics.
- Builds those trees using `cons`, `symbol`, `nil` (and `keyword` where the compiler expects it) so the compiler sees the same shapes it sees from the `.ki` reader.

So: **transformative parser = reader that turns Python AST into Koskari expressions.** The rest of the pipeline is unchanged.

## Implementation Ideas

### 1. Transformative parser (AST → form tree)

- **Input**: Python AST (e.g. `ast.Module`). Obtain via `ast.parse(source)` in the loader.
- **Output**: A sequence of top-level forms, each a **pair tree** (proper list) built with `cons`/`symbol`/`nil` from `koskari.lib`. Each form must be a valid compiler input (symbol or pair; pairs with symbol head for specials or expression form for `compile/call`).
- **Mapping (representative)**:
  - `ast.Module(body=[...])` → one form per statement (or per logical unit); order preserved.
  - `ast.FunctionDef(name, args, body)` → `(define name (lambda (formals...) (begin body_forms...)))`.
  - `ast.ClassDef(name, bases, body)` → `(compile/class name (bases...) body_forms...)`.
  - `ast.Call(func, args, keywords)` → `(compile/call callee_form arg_forms...)` with keyword args in the shape the call special expects (e.g. keyword pairs in the tail).
  - `ast.If(test, body, orelse)` → `(compile/cond (test_form then_forms...) (else: else_forms...))`.
  - `ast.Assign(targets, value)` (top-level or in block) → `(define ...)` or `(setq ...)` depending on scope.
  - `ast.Expr(value)` → the single expression form for that value.
  - `ast.With`, `ast.Try`/`ast.Raise`, `ast.For`/`ast.While` → `(with ...)`, `(compile/try ...)` / `(raise ...)`, `(for-each ...)` / `(compile/while ...)` as appropriate.
  - Attributes / subscripts → `(compile/attr ...)`, `(compile/item ...)`, set/del variants.
  - `ast.Import` / `ast.ImportFrom` → `(compile/import ...)` / `(compile/import/from ...)`.

- **Data structures**: The compiler and specials use `is_pair` and unpacking (`head, tail = source_obj`). The parser must therefore build **real pairs** (and `nil`) from the Koskari lib (e.g. `cons(symbol("compile/call"), cons(fn_form, args_list))`), not Python lists or tuples.

### 2. Loader integration

- **Option A – Same loader, different suffix**: Register a loader for `.py` (or a dedicated suffix like `.py.ki`) that: reads source → `ast.parse` → creates an iterator (or list) of top-level forms from the transformative parser; then runs the same loop as `load_module_demand`: for each form, `compiler.full_compile(form)` and execute in the module namespace. Module creation and `init_module` stay as today.
- **Option B – Pluggable “expression source”**: Generalize the load loop so it accepts an abstract “expression source” (iterator of forms) instead of a file stream. The `.ki` path uses LayoutReader over a file; the Python path uses the transformative parser over the AST. Same `full_compile` + execute loop for both.

### 3. Scope and v1

- Start with a **subset** of Python: assignments, function/class definitions, conditionals, loops, calls, try/raise, with, imports, basic expressions. Comprehensions, decorators, and async can be lowered to existing specials (e.g. comprehensions → loops + accumulation; decorators → `(define name (call decorator (lambda ...)))`) or deferred.
- Preserve **source positions** where possible (e.g. attach to constructed pairs if the pair type supports it) so diagnostics point back to the original Python source.

## Mapping Summary (Python AST → Koskari form)

| Python AST           | Koskari form (conceptual) |
|----------------------|----------------------------|
| Module(body)         | One form per top-level stmt |
| FunctionDef          | `(define name (lambda (params...) (begin body...)))` |
| ClassDef             | `(compile/class name (bases...) body...)` |
| Call                 | `(compile/call callee args...)` |
| If                   | `(compile/cond (test then...) (else: else...))` |
| Assign               | `(define ...)` / `(setq ...)` by scope |
| Expr                 | Inner expression form only |
| With                 | `(with ...)` (special_with) |
| Try / Raise          | `(compile/try ...)` / `(raise ...)` |
| For / While         | `(for-each ...)` / `(compile/while ...)` |
| Attribute / Subscript | `(compile/attr ...)` / `(compile/item ...)` (and set/del) |
| Import / ImportFrom  | `(compile/import ...)` / `(compile/import/from ...)` |

## Dependencies

- **Koskari lib**: `cons`, `symbol`, `nil`, `keyword`, and the `pair` type so that `is_pair`/`is_proper` hold for constructed trees.
- **Compiler and specials**: No changes; they already accept any proper list whose head is a symbol (or an expression form for calls).
- **Import system**: A new finder/loader (or extension of the existing one) to recognize `.py` (or chosen suffix) and drive the AST → form → compile → execute path; see [api/import-system.md](api/import-system.md).

## References

- [api/import-system.md](api/import-system.md) — Module lifecycle, load paths, loader/finder.
- [SPECIALS.md](SPECIALS.md) — Catalog of special forms the transformative parser must target.
- [interpreter/compiler.md](interpreter/compiler.md) — Compiler input (symbol/pair/const), `full_compile`, dispatch to specials.
- Discussion: Python AST and Koskari specials are sufficient to define the transformation; the new piece is the reader/transformative parser; compiler and specials are reused as-is.
