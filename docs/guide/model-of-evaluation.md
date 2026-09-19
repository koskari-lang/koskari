# Model of evaluation

What happens between the text of a program and its result, in four
stages: read, expand, compile, evaluate. Everything else in the guide is
a closer look at one of them.

## Forms

A **form** is one complete piece of Koskari. The everyday shapes:

```koskari
42                         ;; a value
message                    ;; a name
(+ 1 2)                    ;; a call
(print "area:" (* 6 7))    ;; a call with a call inside it
```

A value evaluates to itself. A name evaluates to what it is bound to. A
call evaluates its arguments, then applies the thing in first position
to them. Inner calls finish before the call around them, so
`(print "area:" (* 6 7))` prints `area: 42`.

A file is a sequence of forms, run from top to bottom:

```koskari
(define width 6)
(define height 7)
(print "area:" (* width height))
```

Layout is another spelling of the same forms. A line with more than one
element is a call, and a line ending in `:` opens a block whose indented
lines are arguments to it:

```koskari
define width 6
if (> width 5):
  print "wide"
else:
  print "narrow"
```

Nothing after the reader can tell which spelling was used. The rules,
each beside the s-expression it produces, are in
[Koskari and s-expressions](layout-and-s-expressions.md).

## Read

The reader turns text into forms: atoms, pairs, and the collection
literals. `(+ 1 2)` becomes a pair chain whose head is the symbol `+`.
`"hello"` becomes a string and `42` an integer, since those need no
further work. Layout blocks are read into the same pair chains through
the layout handlers, so a block `if` and a written `(cond ...)` arrive at
the next stage as the same thing.

Reading is strict. A missing `)` or a bad indent is a `ReaderSyntaxError`,
raised before anything runs. The reader also carries source positions,
which is what puts a Koskari line in a traceback.

## Expand

The compiler walks each form. When a form's head names a macro, the
macro receives the form and returns a new one, and the walk continues
on the result. `if` is a macro over `cond`; `when`, `unless`, `set!`,
`define-class`, and the comprehension forms `each`, `list-each`, and
`dict-each` are macros too:

```koskari
>>> (macroexpand '(when ready (print "go")))
(cond [ready (print "go")] [else: None])
```

`for-each` is a compiler special rather than a macro; the comprehension
macros expand into it, and it expands no further.

Expansion is where a program's vocabulary grows. A `define-syntax` in
your own module adds a head the walk knows about, and its uses lower to
ordinary forms before compilation. See
[Macros and hygiene](macros-and-hygiene.md).

## Compile

Once a form has no macro left in it, the compiler emits bytecode for it.
The heads it handles itself are the **special forms**: `define`,
`lambda`, `cond`, `try`, `while`, the operators, the imports, and about
a hundred more, each with a page in the
[reference](../reference/specials/index.md). Anything else in head
position is a call of a runtime value.

The picture that explains most behavior is a stack. For `(+ 1 2)` the
compiler emits: push the constant `1`, push the constant `2`, add two
operands. For `(print (+ 1 2))`: look up `print`, then the three
instructions above, then call with one argument. The inner call leaves
its value on the stack where the outer call finds it, which is why
evaluation is inside out.

Compile errors are shape errors: a `define` with no name, a `lambda`
whose formals are not a list, a form that matches none of a special's
patterns. They are raised as `CompilerError` before the module runs.

```koskari
(define)
```

reads fine and fails here.

## Evaluate

The evaluator runs the bytecode. It is stackless: a Koskari call does
not consume the C stack, so tail calls loop, deep recursion does not
overflow, and a running function can be suspended and resumed. That
last property is what makes every function able to await without being
marked async; see [Demand, async, and defer](demand-async-and-defer.md).

Names are looked up in the namespace the code was compiled in, then its
parents, then the module's builtins. A name with no binding is a
`NameError` at the point of use, not at compile time, because a later
form or another module may bind it first. See
[Name resolution and prefixes](name-resolution-and-prefixes.md) for how
`(+ 1 2)` becomes an opcode while `+` is also a function you can pass
around.

Runtime errors are Python exceptions. `(/ 1 0)` raises
`ZeroDivisionError` from the same place Python would.

## Which stage failed

When a program surprises you, the traceback names the stage:

| Error | Stage | What to look at |
|-------|-------|-----------------|
| `ReaderSyntaxError`, `LayoutError` | read | punctuation, indentation, a `:` inside parentheses |
| `SyntaxMatchError` on a macro | expand | the form against the macro's patterns; `macroexpand-1` shows one step |
| `CompilerError`, `SpecialError` | compile | the form named in the message against its reference page |
| `NameError`, `TypeError`, any Python exception | evaluate | the value, the missing name, the failing call |

The exception classes are catalogued in the
[Error guide](../reference/error-codes.md).

## Data and code

A form is also a value. Quote it and it stays a pair chain instead of
running:

```koskari
>>> '(+ 1 2)
(+ 1 2)
>>> (car '(+ 1 2))
+
```

That is the property macros rest on. A macro is a function whose input
and output are forms, and `car`, `cdr`, and `cons` are how it takes them
apart and puts them together. [Data structures](data-structures.md)
covers the pair and the quoting forms.
