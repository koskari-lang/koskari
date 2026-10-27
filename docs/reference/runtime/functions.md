# Functions

The general functions bound in every module. Each is a value: it can be
called, passed to `map` or `apply`, and stored. Several names are also
special forms or macros, which compile inline when they head a form;
those entries carry an **Also:** line. Types and their `NAME?`
predicates are on [Types and type predicates](types-and-predicates.md),
and the operators on [Operators](operators.md).

## Predicates

**Syntax:** `(instance? VALUE TYPE ...)`, `(subclass? CLASS TYPE ...)`

Python's `isinstance` and `issubclass`: true when the first argument is
an instance, or a subclass, of any **`TYPE`**. A tuple of types is
accepted as one **`TYPE`**.

**Also:** special forms [`instance?`](../specials/compile-instance.md),
[`subclass?`](../specials/compile-subclass.md).

```koskari
(instance? 5 int)              ; True
(instance? 5 #(str int))       ; True
(instance? '(1 2) list)        ; False
(subclass? bool int)           ; True
```

**Syntax:** `(callable? VALUE)`

True when **`VALUE`** can be called.

**Syntax:** `(None? VALUE)`

True only for `None`. `nil` is not `None`.

**Syntax:** `(nil? VALUE)`

True for `nil` and `bracket-nil`, false for everything else, including
`None` and empty Python collections.

**Also:** macros [`None?` and `nil?`](../macros/compile-none-and-compile-nil.md).

**Syntax:** `(proper? VALUE)`, `(recursive? VALUE)`

`proper?` is true for a pair chain ending in `nil`, including `nil`
itself; a Python list is not proper. `recursive?` is true for a pair
chain that loops back on itself.

**Syntax:** `(all? ITERABLE)`, `(any? ITERABLE)`

True when every element, or any element, is truthy. `(all? #[])` is
true and `(any? #[])` is false.

**Syntax:** `(hasattr? OBJ NAME)`

True when **`OBJ`** has the attribute **`NAME`**.

**Syntax:** `(on-demand?)`

Whether the current context is on the demand path. See
[on-demand?](on-demand.md).

```koskari
(callable? +)                  ; True
(None? nil)                    ; False
(nil? '())                     ; True
(nil? #[])                     ; False
(proper? (cons 1 2))           ; False
(any? (map (lambda [x] (> x 2)) #[1 3]))   ; True
(hasattr? 5 "real")            ; True
```

## Pairs

`nil` is the empty list. `(pair? nil)` is true; test `nil?` to tell it
apart.

**Syntax:** `(cons VALUE ... TAIL)`

A chain of pairs holding each **`VALUE`**, ending in **`TAIL`**. `nil`
as the tail makes a proper list; any other tail an improper one.

**Also:** special form [`cons`](../specials/compile-cons.md).

**Syntax:** `(build-proper VALUE ...)`

A proper list of the values; with none, `nil`.

```koskari
(cons 1 2 3 nil)               ; (1 2 3)
(cons 1 2)                     ; (1 . 2)
(cons 0 *: #[1 2] nil)         ; (0 1 2)
(build-proper 1 2 3)           ; (1 2 3)
(apply build-proper #[1 2])    ; (1 2)
```

**Syntax:** `(car PAIR)`, `(cdr PAIR)`

The head and the tail of a pair. Both raise `TypeError` on `nil` and on
any value that is not a pair.

**Also:** special forms [`car`](../specials/compile-car.md),
[`cdr`](../specials/compile-cdr.md).

**Syntax:** `(cadr P)`, `(caddr P)`, `(cadddr P)`, `(caddddr P)`,
`(cddr P)`, `(cdddr P)`, `(cddddr P)`

Compositions of `car` and `cdr`: `cadr` is `(car (cdr P))`, `cddr` is
`(cdr (cdr P))`, and so on.

**Also:** macros [`cadr` and family](../macros/compile-cadr-caddr-cadddr-caddddr.md).

**Syntax:** `(first P)`, `(second P)`, `(third P)`, `(fourth P)`, `(fifth P)`

Aliases for `car`, `cadr`, `caddr`, `cadddr`, and `caddddr`.

```koskari
(car '(1 2 3))                         ; 1
(cdr '(1 2 3))                         ; (2 3)
(cddr '(1 2 3))                        ; (3)
(third '(1 2 3))                       ; 3
(list (map car #['(1 2) '(3 4)]))      ; #[1 3]
```

**Syntax:** `(set-car PAIR VALUE)`, `(set-cdr PAIR VALUE)`

Replace the head or the tail of **`PAIR`** in place. `nil` cannot be
modified and raises `TypeError`. `set!` accepts `car`, `cdr`, and the
`c[ad]+r` forms as places and rewrites to these calls.

```koskari
var p (cons 1 2 3 nil)
(set-car p 9)                  ; p is (9 2 3)
set! (cadr p) 8                ; p is (9 8 3)
(set-cdr p nil)                ; p is (9)
```

A pair iterates as its two fields, the car and the cdr. `len`, `list`,
`for-each`, `map`, and `*:` all see those two items, not the elements
of the chain:

```koskari
(len '(1 2 3))                 ; 2
(list '(1 2 3))                ; #[1 (2 3)]
```

**Syntax:** `(unpack VALUE)`

An iterator over the elements of a pair chain. Any other iterable is
passed to `iter`.

```koskari
(list (unpack '(1 2 3)))       ; #[1 2 3]
(sorted (unpack '(3 1 2)))     ; #[1 2 3]
```

Pattern binding walks the chain: `(var [a b] '(1 2 3))` binds `a` to
`1` and `b` to `(2 3)`.

**Syntax:** `(bracket-cons VALUE ... TAIL)`

`cons` producing bracket pairs, the pairs the reader builds for `[ ]`
forms. A bracket pair compares equal to a plain pair and works with
every function above. **`bracket-nil`** is the empty `[]`, and
`(nil? bracket-nil)` is true. See
[Brackets and the call convention](../../guide/brackets-and-call-convention.md).

**Also:** special form [`bracket-cons`](../specials/compile-bracket-cons.md).

**Syntax:** `(bracket-splice SEGMENT ...)`

Concatenate the elements of each **`SEGMENT`** -- a pair chain, `nil`,
or any iterable -- into one list whose head is a bracket pair. With no
elements, `bracket-nil`.

```koskari
(== (bracket-cons 1 2 bracket-nil) '(1 2))     ; True
(bracket-splice '(1 2) #(3 4) nil)             ; [1 2 3 4]
(is? (bracket-splice) bracket-nil)             ; True
```

## Collection builders

The reader forms `#[...]`, `#(...)`, and `#{...}` read to calls of
`#list`, `#tuple`, and `#dict`; see
[Data structures](../../guide/data-structures.md). Each builder accepts
`*:` to splice an iterable into its arguments.

**Also:** special forms
[`#list`, `#tuple`, `#list-append`](../specials/object-operators/index.md#collection-builders),
[`#str`](../specials/compile-str.md).

**Syntax:** `(#list VALUE ...)`, `(#tuple VALUE ...)`, `(#set VALUE ...)`

A list, tuple, or set of the arguments; with none, an empty collection.
There is no set literal. The builders take the elements as arguments;
the types `list`, `tuple`, and `set` take one iterable.

```koskari
(#list 1 2 3)                  ; #[1 2 3]
(#tuple)                       ; #()
(sorted (#set 1 2 2))          ; #[1 2]
(apply #tuple (range 3))       ; #(0 1 2)
(#list *: (range 3))           ; #[0 1 2]
```

**Syntax:** `(#dict ITEM ... KEY: VALUE ...)`

A dict from key-value items and keyword arguments. Each **`ITEM`** is a
two-element sequence `#(KEY VALUE)`; a keyword argument adds its name
as a string key. Later entries replace earlier ones with the same key.
`#{K: V ...}` reads to `(#dict #(K V) ...)`. A bare key and value,
`(#dict "a" 1)`, raises `ValueError`.

```koskari
(#dict #("a" 1) #("b" 2))      ; #{"a": 1 "b": 2}
(#dict a: 1)                   ; #{"a": 1}
(#dict #("a" 1) #("a" 2))      ; #{"a": 2}
```

**Syntax:** `(#str VALUE ...)`

The `str` of each argument, concatenated; with none, `""`. F-strings
read to `#str` calls.

```koskari
(#str 1 "a" 2)                 ; "1a2"
(#str None True)               ; "NoneTrue"
```

**Syntax:** `(#list-append LIST VALUE)`

Append **`VALUE`** to **`LIST`** in place. Exactly two arguments.
Called as a function value, it returns `None`.

## Calling

**Syntax:** `(apply FUNC [ARGS [KWARGS]])`

Call **`FUNC`** with the positional arguments in the iterable
**`ARGS`** and the keyword arguments in the dict **`KWARGS`**. There
are no leading positional arguments before **`ARGS`**; put every
positional argument in it.

```koskari
(apply + #[1 2])                                   ; 3
(apply (lambda [a b: 0] (+ a b)) #[1] #{"b": 5})   ; 6
(apply (lambda [] 7))                              ; 7
```

**Syntax:** `(identity VALUE)`, `(constantly VALUE)`

`identity` returns **`VALUE`**. `constantly` returns a function that
ignores its arguments and returns **`VALUE`**.

```koskari
(list (filter identity #[0 1 "" "x"]))   ; #[1 "x"]
((constantly 5) 1 2 x: 3)                ; 5
```

**Syntax:** `(partial FUNC ARG ... KEY: VALUE ...)`

A function that calls **`FUNC`** with the given arguments followed by
its own. A Koskari function passed to it runs inside the evaluator and
may await; `functools.partial` over a Koskari function returns an
unawaited coroutine when Python calls it.

```koskari
((partial + 10) 5)                                 ; 15
((partial (lambda [a b: 0] (+ a b)) b: 2) 1)       ; 3
```

**Syntax:** `(demand FUNC)`, `(demand-call FUNC ARG ...)`,
`(demand-apply FUNC ARGS [KWARGS])`

Run **`FUNC`** on the demand path, where it returns a value or raises
and never suspends; a suspension raises `KoskariSyncViolation`.
`demand` wraps **`FUNC`** and is usable as a decorator, `@demand`.
`demand-call` calls it with the arguments; `demand-apply` with the
arguments in a tuple and an optional dict. See
[Demand, async, and defer](../../guide/demand-async-and-defer.md).

```koskari
((demand (lambda [x] (* x 3))) 2)                          ; 6
(demand-call (lambda [a] (* a 2)) 4)                       ; 8
(demand-apply (lambda [a b: 0] (- a b)) #(5) #{"b": 1})    ; 4
```

**Syntax:** `(values VALUE ...)`

Multiple results as one object. Pattern binding unpacks it.

```koskari
var [a b] (values 1 2)
(+ a b)                        ; 3
```

**Syntax:** `(decorate-wraps INNER WRAPPER)`

Copy the name, docstring, and other metadata of **`INNER`** onto
**`WRAPPER`** and return **`WRAPPER`**, as `functools.update_wrapper`
does. When **`INNER`** is a coroutine function, **`WRAPPER`** is first
passed through `coroutine-fn`. See [Decorators](../../guide/decorators.md).

## Async

**Syntax:** `(defer THUNK)`

A single-shot awaitable over the call of **`THUNK`**. It is not
awaited at the next use; an explicit `(await ...)` consumes it, and a
second await raises `RuntimeError`.

**Syntax:** `(create-task AWAITABLE)`

Schedule **`AWAITABLE`** on the running loop and return the
`asyncio.Task`. A task is not auto-awaited; `(await TASK)` resolves it.
Passing a defer consumes it.

**Syntax:** `(coroutine-fn FUNC)`

Wrap **`FUNC`** so `asyncio.iscoroutinefunction` accepts it. Usable as
a decorator, `@coroutine-fn`.

**Syntax:** `(run-process ARG ...)`

Run the argv in a subprocess to completion and return a
`ProcessResult` holding `args`, `returncode`, `stdout`, and `stderr`.
Its `text`, `stderr_text`, `json`, `jsonl`, and `yaml` decode the
output, and `raise_for_status` raises `CalledProcessError` on a nonzero
exit. A nonzero exit is not raised otherwise. See
[`fence/bash`](../fences/fence-bash.md).

```koskari
(await (defer (lambda [] 4)))                              ; 4

(import asyncio)
(asyncio.iscoroutinefunction (coroutine-fn (lambda [] 1))) ; True
```

## Iteration

**Syntax:** `(iter ITERABLE)`, `(next ITERATOR [DEFAULT])`,
`(next ITERATOR default: DEFAULT)`

An iterator over **`ITERABLE`**, and its next item. An exhausted
iterator returns **`DEFAULT`** when given, and otherwise raises
`StopIteration`.

**Also:** special forms
[`iter`, `next`](../specials/object-operators/index.md#unary-and-inspection-operators).

```koskari
var it (iter #[1])
(next it)                      ; 1
(next it 9)                    ; 9
(next it default: 9)           ; 9
```

**Syntax:** `(sorted ITERABLE key: KEY reverse: REVERSE)`

A new sorted list. **`KEY`** is wrapped in `demand`, so a Koskari
function works as the key.

```koskari
(sorted #["aa" "b"] key: (lambda [s] (len s)))    ; #["b" "aa"]
(sorted #[3 1 2] reverse: True)                   ; #[3 2 1]
```

**Syntax:** `(max ITERABLE key: KEY)`, `(max VALUE ...)`, and `min` the
same

The largest or smallest element. A Koskari function passed as `key:`
returns an unawaited coroutine, which does not compare; wrap it in
`demand`.

```koskari
(max 3 7 5)                                            ; 7
(max #["aa" "b"] key: len)                             ; "aa"
(min #["aa" "b"] key: (demand (lambda [s] (len s))))   ; "b"
```

**Syntax:** `(len COLLECTION)`

The number of elements. A pair chain counts as two.

**Also:** special form
[`len`](../specials/object-operators/index.md#unary-and-inspection-operators).

**Syntax:** `(sum ITERABLE [START])`, `(enumerate ITERABLE [START])`,
`(reversed SEQUENCE)`

The sum of the elements plus **`START`**; pairs of index and element
counting from **`START`**; a reverse iterator.

```koskari
(sum #[1 2] 10)                        ; 13
(list (map list (enumerate "ab" 1)))   ; #[#[1 "a"] #[2 "b"]]
(list (reversed (range 3)))            ; #[2 1 0]
```

`map`, `filter`, and `zip` are lazy iterators that accept Koskari
functions; see [map and filter](map-and-filter.md). `py-map`,
`py-filter`, and `py-zip` are Python's.

## Conversion and display

**Syntax:** `(repr VALUE)`, `(py-repr VALUE)`

`repr` writes Koskari syntax for symbols, pairs, and the other Koskari
types; `py-repr` is Python's `repr`.

**Also:** special form
[`repr`](../specials/object-operators/index.md#unary-and-inspection-operators).

```koskari
(repr '(1 2))                  ; "(1 2)"
(py-repr '(1 2))               ; "cons(1, 2, nil)"
(repr 'a)                      ; "a"
(py-repr 'a)                   ; "<symbol 'a'>"
```

**Syntax:** `(format VALUE [SPEC])`

**`VALUE`** formatted by the format spec **`SPEC`**.

**Also:** special form [`format`](../specials/compile-format.md).

| Name | Does |
|---|---|
| `(abs X)` | Absolute value |
| `(round X [DIGITS])` | Rounded, halves to even |
| `(bin N)`, `(hex N)`, `(oct N)` | An integer as a binary, hexadecimal, or octal string |
| `(chr N)`, `(ord C)` | Code point to one-character string, and back |
| `(ascii VALUE)` | `repr` with non-ASCII characters escaped |
| `(hash VALUE)` | The hash of a value |
| `(id VALUE)` | The identity of an object |

```koskari
(format 3.14159 ".2f")         ; "3.14"
(abs -3)                       ; 3
(round 2.5)                    ; 2
(hex 255)                      ; "0xff"
(chr 65)                       ; "A"
```

## Input and output

**Syntax:** `(print VALUE ...)`, `(input [PROMPT])`, `(open PATH [MODE])`

Write the values to standard output; read a line from standard input;
open a file.

## Attributes and items

**Syntax:** `(getattr OBJ NAME [DEFAULT])`, `(setattr OBJ NAME VALUE)`

Read or write the attribute **`NAME`**. A missing attribute returns
**`DEFAULT`** when given.

**Syntax:** `(vars OBJ)`, `(dir OBJ)`

The `__dict__` of **`OBJ`**, and its attribute names.

**Also:** special forms [`vars`](../specials/compile-vars.md),
[`dir`](../specials/object-operators/index.md#unary-and-inspection-operators).

**Syntax:** `(itemgetter KEY ...)`, `(attrgetter NAME ...)`

A function of one argument that returns its item **`KEY`**, or its
attribute **`NAME`**. With several keys or names, a tuple.

```koskari
(getattr 5 "nope" 0)                               ; 0
(sorted #[#(2 "b") #(1 "a")] key: (itemgetter 0))  ; #[#(1 "a") #(2 "b")]
((itemgetter 1 0) #[10 20])                        ; #(20 10)
((attrgetter "real") 5)                            ; 5
```

**Syntax:** `(super [TYPE OBJ])`

The parent-class proxy, inside a method.

**Also:** macro [`super`](../macros/compile-super.md).

| Name | Page |
|---|---|
| `getattr*`, `hasattr*` | [getattr* and hasattr*](getattrstar-and-hasattrstar.md): nested attribute access along a dotted path |
| `getitem*`, `hasitem*` | [getitem* and hasitem*](getitemstar-and-hasitemstar.md): nested item access along a key path |
| `build-slice` | [build-slice](build-slice.md): a slice object, the runtime behind `item-slice` |
| `koskari-vars` | [koskari-vars](koskari-vars.md): the calling frame's namespace, or an object's `__dict__` |

## Refs, parameters, and the environment

**Syntax:** `(deref REF)`

The value a ref holds. See [`ref`](../macros/compile-ref.md).

**Also:** special form [`deref`](../specials/compile-deref.md).

**Syntax:** `(parameter NAME [DEFAULT])`

The value bound to the parameter **`NAME`**: the innermost
`parameterize` first, then the root scope. **`DEFAULT`** answers when
nothing is bound; with no default an unbound name raises `NameError`.

**Also:** special form [`parameter`](../specials/compile-parameter.md).

**Syntax:** `(environ-ref KEY [DEFAULT])`

A ref to the environment variable **`KEY`**. Without **`DEFAULT`** it
is an `ItemRef`, and reading an unset variable raises `KeyError`. With
**`DEFAULT`** it is a `DynamicRef`: reading an unset variable returns
**`DEFAULT`**, and clearing the ref, as `take!` on a name bound to it
does, deletes the variable. An empty key raises `ValueError`. The
function behind [`define-environ`](../macros/compile-define-environ.md).

```koskari
var v 5
(deref &v)                             ; 5
(apply parameter #['nope 3])           ; 3
var mode (environ-ref 'APP_MODE "dev")
(deref mode)                           ; "dev" while APP_MODE is unset
```

## Compilation and expansion

| Name | Page |
|---|---|
| `compile` | [compile](compile.md): compile a source form without running it |
| `evaluate` | [evaluate](evaluate.md): compile a form if needed and run it |
| `get-compiler` | [get-compiler](get-compiler.md): the compiler that owns a namespace |
| `macroexpand`, `macroexpand-1` | [macroexpand](macroexpand.md), [macroexpand-1](macroexpand-1.md) |
| `apply-layout`, `apply-layouts` | [block layout expansion](block-quasiquote-and-block-layout-expansion.md): expand `#block:` forms at run time |
| `help` | [help](help.md): documentation through the pager |

**Also:** macros [`evaluate`](../macros/compile-evaluate.md),
[`get-compiler`](../macros/compile-get-compiler.md),
[`macroexpand`](../macros/compile-macroexpand.md),
[`help`](../macros/compile-help.md).

**Syntax:** `(gensym [NAME])`

A fresh symbol, distinct from every other symbol including other
gensyms of the same name.

```koskari
(== (gensym "x") (gensym "x"))         ; False
```

**Syntax:** `(active-pragma [KEY])`

The pragma of the module being compiled, or the value of its **`KEY`**
setting; outside a compilation turn, a default pragma. Called as a
function value, the key is the keyword argument `key:`. See
[Pragmas and feature settings](../../guide/pragma.md).

**Syntax:** `(help-pragma-key KEY)`

Show the documentation for the pragma setting **`KEY`** through the
pager; `(help (pragma KEY))` calls it. An unknown key raises
`ValueError`.

**Syntax:** `build-syntax`, `build-macro`, `build-special`,
`build-bare-syntax`

The functions the syntax-defining forms compile to:
[`syntax-rules`](../specials/compile-syntax-rules.md) and
[`syntax-match`](../specials/compile-syntax-match.md) to `build-syntax`,
[`macro-rules`](../specials/compile-macro-rules.md) to `build-macro`,
[`special-match`](../specials/compile-special-match.md) to
`build-special`, and [`bare-syntax`](../specials/compile-bare-syntax.md)
to `build-bare-syntax`. Their arguments are matcher rows the compiler
produces; write the defining forms instead of calling these.

## Constants

`None`, `True`, `False`, `Ellipsis`, `...`, and `NotImplemented` are
Python's constants; `(is? Ellipsis ...)` is true. `nil` and
`bracket-nil` are the empty lists; see [Pairs](#pairs).
