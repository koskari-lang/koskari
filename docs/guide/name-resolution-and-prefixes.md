# Name resolution and prefixes

One name can mean up to three things, chosen by a prefix and by where
the name appears. That is what lets `(+ 1 2)` compile to arithmetic
opcodes while `+` is also a function that can be passed around. The two
prefixes are `compile/` and `layout/`.

## The three slots

A name such as `foo` can fill up to three independent slots:

| Slot | Written as | Consulted when | Holds |
|------|-----------|----------------|-------|
| runtime | `foo` | `foo` is used as a value, or called through `compile/call` | a value, usually a function |
| compile-time | `compile/foo` | `(foo ...)` appears in operator position | a syntax: a special form or macro that runs at compile time |
| layout | `layout/foo` | `foo` heads an indented block, `foo ...:` | a layout handler that turns the block into a form |

A name fills one, two, or all three, and whoever defines the name
decides which.

## How `(foo ...)` resolves

When the compiler sees a list whose head is a symbol, `(foo a b)`, it
resolves `foo` in this order:

1. If `compile/foo` is bound to a syntax, use it. A syntax whose only
   arm is a bare form acts as an alias here: the head expands through
   the bare arm and the arguments ride along, so `(foo a b)` becomes
   `(EXPANSION a b)` and resolves again from the top.
2. Otherwise, rewrite the call to `(call foo a b)`, a runtime call of
   whatever value `foo` holds.

The short name is the only spelling that resolves as a use. The prefixed
spelling in operator position, `(compile/foo a b)`, is a runtime call of
the syntax object, which fails. The prefixed spelling always names the
object itself.

When the head is a form rather than a symbol, the compiler first tries
to lower it; see "Expansion in operator position" below.

### The two `+`

`+` fills the runtime and the compile-time slots:

```koskari
(+ 1 2)          ; step 1 finds compile/+, which emits opcodes
(var p +)     ; + as a value: the runtime function
(p 3 4)          ; 7
```

`(+ 1 2)` never calls the `+` function; the `compile/+` syntax turns it
into opcodes. The function `+` is a separate value in the runtime slot,
reached when `+` is used as a value or through `call`.

## A name as a value

Outside operator position a bare name resolves to its runtime slot:

```koskari
import functools
(functools.reduce + xs)    ; + here is the runtime function, passed as a value
```

A name that fills only its compile-time slot has nothing in the runtime
slot, so using it as a value is an error:

```koskari
(if True 1 2)    ; compile/if is a syntax
(var x if)    ; NameError: No such reference: if
```

A syntax with a bare form never reaches value resolution: the lone
symbol expands before the runtime slot is consulted. The statement words
`return`, `continue`, `break`, `raise`, `yield`, and `pass` carry
bootstrap bare forms, so a lone `return` expands to `(return)`, the
Python-style spelling works, and none of the six can name a variable.
The [`compiler/bare-control-words`](pragma.md#compiler) pragma selects
whether that conversion is silent, warned, or refused. Any syntax can
declare a bare form with the `bare-syntax` transformer kind; see
[`bare-syntax`](../reference/specials/compile-bare-syntax.md).

## Forcing a runtime call

`(call foo a b)` compiles through `compile/call`, bypassing any
`compile/foo` syntax, and calls the runtime value of `foo`:

```koskari
(+ 1 2)          ; compile/+: opcodes
(call + 1 2)     ; compile/call: a call of the runtime +, also 3
```

`call` is needed in self-referential syntax. A macro named `foo` often
expands into code that must call the function `foo`, for example a
wrapper that supplies default arguments from the caller's `(globals)`.
If the expansion wrote `(foo ...)`, resolution would find the macro
again and expand forever. `(call foo ...)` reaches the runtime function
and ends the loop. Outside that case, `call` is rarely written.

## The layout slot

An indented block whose header is `foo` consults `layout/foo`, a handler
that rewrites the block into a form before the rules above apply.
`while` fills both a layout slot and a compile-time slot:

```koskari
while (> n 0):        ; layout/while turns the block into (while ...)
  (setq n (- n 1))

(while (> n 0) (setq n (- n 1)))   ; the same form, written directly
```

The layout slot affects only how the block is read. The resulting form
resolves through the compile-time and runtime rules like any other.

## Filling the slots

| Definition | Result | Example |
|------------|--------|---------|
| a runtime value and a syntax | `(foo ...)` uses the syntax; `foo` as a value, or `(call foo ...)`, uses the function | `+` |
| a syntax only | `(foo ...)` expands, and `foo` has no runtime value | `if`, `while`, `begin`, most `syntax` macros |
| either, plus `layout/foo` | the block header `foo ...:` is handled as well | `while`, `try`, `class` |

`syntax foo:` and `syntax compile/foo:` are the same
definition: the transformer is bound under `compile/foo` whichever is
written. The short spelling is the one to use; the written name is the
transformer's identity in help text and match errors, and the short
name is what every use site writes.

## Expansion in operator position

A head can be a form. When it is, the compiler tries to lower it before
falling back to a call: if the head's innermost head names a syntax
whose expansion yields source, the head is replaced by its expansion and
the whole form is resolved again from the top.

```koskari
(syntax sel (syntax-rules [] [(sel x) +]))

((sel 1) 2 3)      ; the head (sel 1) expands to the symbol +
                   ; (+ 2 3) is 5, compiled as the arithmetic special
```

An expansion that lands on a symbol makes the rebuilt form a fresh
syntax use with the tail as its arguments. Scheme expands an
application's operator as an expression and never re-examines it as
syntax; in Koskari a macro can compute the syntax at the head of a form.

Attempts nest to any depth and are all or nothing. A head whose
innermost head is not a syntax, or whose expansion compiles to bytecode
rather than source, fails the whole attempt, and the original form
lowers to a call of the head's value. That is why `((make-adder 1) 2)`
with a plain function means what it always did, and why a method call's
`(attr obj name)` head reaches the call machinery intact. Nothing from a
failed attempt is kept.

```koskari
(syntax foo2 (syntax-rules [] [(foo2 x) bar2]))
(syntax bar2 (syntax-rules [] [(bar2 x) +]))

(((foo2 1) 2) 3)   ; ((bar2 2) 3), then (+ 3), then 3
```

## The syntax object

Because the prefixed spelling never resolves as a use, in value position
it is an ordinary read of the transformer itself:

```koskari
(var x if)              ; NameError: there is no runtime if
(var x compile/if)      ; the compile/if Syntax object
(help compile/if)          ; describe the transformer
```

Tooling and meta-programs hold a transformer this way. The short name is
the use; the prefixed name is the thing.

## Names a macro emits resolve at the use site

A macro template names other identifiers: a class it constructs, a
helper it calls. Those names resolve where the macro is used, not where
it is defined. A macro that expands to `(basicConfig ...)` compiles at
its definition and fails with `No such reference: basicConfig` at every
use site that has not imported `basicConfig`.

A macro shipped in a library must emit only names its caller can reach.
Route the expansion through the library's exported helpers, an exported
`(configure-logging ...)` rather than `basicConfig`, and export both.
Import the macro with [`use`](../reference/specials/use.md), which
carries a name's facets together: `(use your-lib your-macro)` brings the
transformer and any runtime sibling in one member.

The rule behind a working macro library: export everything a macro
expands into. To audit one, read the expansion and check that each free
identifier in it is exported.

## Summary

- Write surface forms: `(+ 1 2)`, `(if ...)`, `(while ...)`, `(await x)`.
  The compiler finds the compile-time form; a `compile/` prefix is
  almost never typed.
- `call` forces a runtime call past a compile-time form, which comes up
  inside a self-referential macro.
- A `compile/foo` head is a mistake: it calls the syntax object as a
  runtime value and fails. In value position it is a reference to the
  transformer.

## See also

- [`compile/call`](../reference/specials/compile-call.md): the default
  call form and its opcode.
- [Brackets and the call convention](brackets-and-call-convention.md):
  what counts as operator position.
- [Demand, async, and defer](demand-async-and-defer.md): the two paths
  the evaluator can take.
