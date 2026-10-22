# Macros

The macros the bootstrap defines. Each expands at compile time to
[special forms](../specials/index.md) or to other macros, and the
expansion is then compiled as if it had been written. `macroexpand` at
the REPL shows the expansion of any of them.

## Binding and assignment

| Macro | Does |
|-------|------|
| [`set!`](compile-set.md) | Store into a name, attribute, subscript, or ref, choosing the special by target shape |
| [`incr!`, `decr!`](compile-incr-and-compile-decr.md) | Add to or subtract from a binding in place |
| [`del!`](compile-del.md) | Delete a name, attribute, or subscript |
| [`take!`](compile-take.md) | Read a name, attribute, or subscript and clear it |
| [`bind!`](compile-bind.md) | Bind a name to a ref, typed by the target shape |
| [`ref`](compile-ref.md) | A ref to a name, attribute, subscript, or parameter |
| [`unassigned?`, `default!`](compile-default-and-compile-unassigned.md) | Whether a location holds a value; store one unless it already does |
| [`global`, `local`](compile-global-and-compile-local.md) | Declare which scope a name belongs to |
| [`let*`](compile-letstar.md) | Sequential `let`, each binding seeing the ones before |
| [`let-values`](compile-let-values.md) | `let` with unpacking |
| [`labels`](compile-labels.md), [`flet`](compile-flet.md) | Local functions for a body, mutually recursive and not |
| [`define-parameter`](compile-define-parameter-and-compile-parameterq.md) | Declare a dynamic parameter |
| [`define-environ`](compile-define-environ.md) | Bind a name to an environment variable |
| [`declare-exports`](compile-declare-exports.md) | Name the module's public features |
| [`entry-point`](compile-entry-point.md) | Mark the function a program runs once its module has loaded |

## Conditionals

| Macro | Does |
|-------|------|
| [`if`](compile-if.md) | Two-armed conditional over `cond` |
| [`when`](compile-when.md), [`unless`](compile-unless.md) | One-armed conditionals |
| [`?`](compile-q.md) | Conditional with a boolean default |
| [`bool`](compile-bool.md) | Truthiness as a boolean |
| [`None?`](compile-none-and-compile-nil.md) | Identity test against the `None` singleton |
| [`has?`](compile-has.md) | Membership |
| [`assert`](compile-assert.md) | Raise `AssertionError` unless a test holds; removable by pragma |
| [`pass`](compile-pass.md) | A no-op expression, and a bare control word |

## Definitions

| Macro | Does |
|-------|------|
| [`define-class`](compile-define-class.md) | Define a class, with a predicate named after it |
| [`define-dataclass`](compile-define-dataclass.md) | A class through `dataclasses.dataclass` |
| [`define-demanding`](compile-define-demanding.md) | A function wrapped for direct calls from Python |
| [`define-coroutine`](compile-define-coroutine.md) | A function that Python sees as a coroutine function |
| [`super`](compile-super.md) | The superclass proxy inside a method |
| [`decorate`](compile-decorate.md), [`decorate-subject`](compile-decorate-subject.md) | What `@` lines lower to, and the per-decorator dispatch |
| [`when-main`](compile-when-main.md) | Python's `__name__` guard; prefer `entry-point` for a program's work |

## Iteration and collections

| Macro | Does |
|-------|------|
| [`each`](compile-each.md) | A generator over a `[var seq]` binding, with `when:` and `unless:` filters |
| [`list-each`](compile-list-each.md) | A list from the same shape |
| [`dict-each`](compile-dict-each.md) | A dict from `key:` and `value:` clauses |
| [`slice`, `item-slice`](compile-slice-and-compile-item-slice.md) | A slice object; a slice of a sequence |
| [`item*`, `attr*`](compile-itemstar-and-compile-attrstar.md) | Nested item and attribute chains |
| [`cadr` ... `caddddr`, `first` ... `fifth`](compile-cadr-caddr-cadddr-caddddr.md) | Positions in a pair chain |
| [`function-compose`](compile-function-compose.md) | Compose functions right to left |

## Macros about macros

| Macro | Does |
|-------|------|
| [`define-syntax`](compile-define-syntax.md) | Define a transformer, with any of the rule and match kinds or a bare form; its template names are kept apart from the use site, see [Hygiene](../../guide/hygiene.md) |
| [`extend-syntax`](compile-extend-syntax.md) | Add arms to an existing transformer |
| [`let-syntax`](compile-let-syntax.md) | A transformer scoped to a body |
| [`inline-macro`](compile-inline-macro.md) | Run a body at expand time and splice its result |
| [`datum`](compile-datum.md) | Capture a form with its source position for hygiene |
| [`datumq`](compile-datumq.md) | The same without evaluating the argument; in a template, writes a form raw (`-'form`) |
| [`define-fence`](compile-define-fence.md) | Define a fence handler for a triple-backtick literal |
| [`macroexpand`, `macroexpand-1`](compile-macroexpand.md) | Expand a form fully, or by one step |
| [`macroexpandq`, `macroexpandq-1`](compile-macroexpandq.md) | The same without quoting the form |
| [`compileq`](compile-compileq.md), [`evaluate`](compile-evaluate.md), [`get-compiler`](compile-get-compiler.md) | Compile, evaluate, and reach the compiler from a program |
| [`pragma`](compile-pragma.md) | Set or read a module's reader and compiler settings |
| [`help`](compile-help.md) | Documentation for a name |

```{toctree}
:maxdepth: 1
:hidden:
:glob:

*
```
