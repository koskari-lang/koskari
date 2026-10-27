# Bindings and refs

A binding connects a name to a storage cell, and a namespace is the table
of those cells. This page covers the surface first, `var`, `def`, `let`,
and `set!`, then the model underneath, where every name resolves through a
ref.

## Defining names

`var` introduces a name in the current namespace:

```koskari
var greeting "hello"
var count 3
print greeting count
```

At the top of a file the namespace is the module's. Inside a function
body it is the function's; a `var` there is local, as an assignment
in a Python function is.

`def` defines a function and binds it the same way:

```koskari
def area [width height]:
  * width height

area 6 7      ;; 42
```

## `var` and `define`

`var`, `def`, `global` and `syntax` are the idiomatic binding forms.
The Scheme spellings remain for code written in that style, and the two
mix freely in one module:

| Koskari | Scheme |
|---|---|
| `var NAME VALUE` | `define NAME VALUE` |
| `def NAME [FORMALS]: BODY` | `define [NAME FORMALS]: BODY` |
| `def NAME :: T [FORMALS]: BODY` | `define [(NAME :: T) FORMALS]: BODY` |
| `var [a b] VALUE` | `define-values [a b] VALUE` |
| `global var NAME VALUE` | `define-global NAME VALUE` |
| `global def NAME [FORMALS]: BODY` | `define-global [NAME FORMALS]: BODY` |
| `global syntax NAME TRANSFORMER` | `define-syntax NAME TRANSFORMER` |

`syntax` inside a body scopes the transformer to the rest of that body;
`global syntax` and `define-syntax` bind it at the module once the form
has run.

`define` takes Scheme's shape: a function's name goes inside the pair
with its formals, as in Scheme's `(define (area w h) ...)`:

```koskari
define [area width height]:
  * width height

area 6 7      ;; 42
```

`def` takes formals and a body, never a plain value; `def x 1` fails to
compile. `var` takes a value, never formals and a body. A pair after
`define` is always a function head, never a pattern: `define [a b] x`
defines a function `a` of one argument `b`, where `var [a b] x` unpacks
`x` into `a` and `b`.

## Local names with `let`

`let` binds names for one body:

```koskari
let [[width 6] [height 7]]:
  (* width height)
```

Outside the body those names are not bound. A `let` inside a function
scopes a helper to one calculation without touching the function's own
names.

## Changing a binding

`set!` stores a new value into a name that is already bound:

```koskari
var counter 0
set! counter (+ counter 1)
```

`incr!` and `decr!` are the same store for the two common updates. Inside
a function, `set!` on a name with no binding in any enclosing scope is a
compile error; declare the name first, with `var` for a local or
`global var` for a module name. At module level the store creates the module name.

An inner function stores into the outer name it finds, so Python's
`nonlocal` has no counterpart:

```koskari
def make-counter []:
  var count 0
  def tick []:
    incr! count
    count
  tick
```

## Shadowing

A local binding can reuse an outer name, and the inner one wins inside
its body:

```koskari
var message "outer"
let [[message "inner"]]:
  print message      ;; inner
print message        ;; outer
```

## Namespaces

A namespace is a lexical scope: a table of names, each tied to a ref.
Modules, `let` bodies, and function frames each have one, chained to the
namespace they were created in.

A namespace answers questions at three tiers, and its Python-facing API
keeps them apart.

| Tier | What it holds | Reached through |
|------|---------------|-----------------|
| 1 | builtins: the names a module falls through to | `ns.builtins`, a read-only mapping (module namespaces only) |
| 2 | refs: the names bound here, each to a ref | `ns.refs`, `ns.ref(name)`, `ns.bind(name, ref)`, `ns.bound(name)` |
| 3 | values: what those refs currently hold | the mapping protocol: `name in ns`, `ns[name]`, `ns.get(name)`, `len(ns)`, `keys()`, `values()`, `items()`, iteration; also `ns.deref(name)` |

Tier 2 is append-only. A name once bound is never unbound, only emptied,
because closures may already hold its ref. `del ns[name]` and
`ns.clear(name)` empty the ref; afterwards `name in ns` is false while
`name in ns.refs` stays true. `ns.ref(name)` on a module namespace
creates a ref when the name is missing, so a request for a ref is the
one read that creates. For a name the builtins hold, the ref starts out
holding that builtin and lives apart from the module's bindings: a
function that reads `max` shares that ref, while `"max" in ns`,
`ns.refs`, `ns.bound("max")`, `dir(module)` and a star import do not
report it. The name becomes a module binding when the module binds it,
by a `var` or `def`, a store, a `global` in one of its functions, or an
attribute store on the module, and the same ref then carries the
module's value to every holder. For any other missing name the new ref
is empty and bound at once. A builtin's ref is the module's own:
storing through it changes the name inside this module only, and later
changes to the builtins are not seen through it.

The mapping protocol never reaches tier 1. A builtin is not `in` a module
namespace, `ns[name]` raises `KeyError` for it, and `ns.get(name)`
returns the default. The lookup a symbol performs when evaluated, this
namespace's value, then each enclosing namespace's, then the module's
builtins, is `ns.resolve(name)`, with an optional default in place of
`NameError`.

A `Module` object's attributes are tier 3 of its namespace: `module.name`
reads a value the module holds and `dir(module)` lists those names.
Builtins are not attributes of a module.

## Ref

A ref is a mutable handle: `deref` reads the current value, `store`
writes, `clear` empties the slot. Refs can represent a local name, an
attribute, a subscript, a parameter, or other custom get and set logic.

Surface forms:

- `(ref sym)`, a ref to a variable ([`compile/ref`](../reference/macros/compile-ref.md))
- Reader `&name` and `&obj.attr`: an atom prefixed with `&` wraps it in `(ref ...)`
- Reader `&(item coll key)`: `&` immediately followed by `(` wraps the parenthesized form in `(ref ...)`
- `(ref (attr obj name))`, `(ref (item coll key))`, parameters, and so on

Bytecode `VAR_REF` pushes the ref for a name; attribute and item refs use
their own opcodes.

## Bind and store

These are different operations.

| Operation | Meaning | Compile form |
|-----------|---------|--------------|
| bind | Attach a name to a ref, introducing or replacing the mapping | [`compile/bindq`](../reference/specials/compile-bindq.md), [`bind!`](../reference/macros/compile-bind.md) |
| store | Write a value through the ref a name already uses | [`compile/setq`](../reference/specials/compile-setq.md), `set!` |

Bind answers "which ref does this name resolve to?" Store answers "what
value is in that ref now?" A name is bound before anything can be stored
into it, and reassignment of an existing variable is a store, not a new
bind.

The [`bind!`](../reference/macros/compile-bind.md) macro can install
typed refs (item, attribute, dynamic, parameter, environ, and others)
while still using bind under the hood. The environ ref reads and writes
a process environment variable through `os.environ`; see
[`compile/define-environ`](../reference/macros/compile-define-environ.md).

## Deref

Deref reads through a ref and yields the value.

- `(deref expr)`, [`compile/deref`](../reference/specials/compile-deref.md)
- The runtime `deref` builtin, when `deref` is wanted as a first-class function

Use `ref` when you need the cell, for example to pass a mutable slot to
another form. Use `deref` when you need the contents.
[References and storage indirection](references-and-storage-indirection.md)
works through both, with `bind!` and by-reference parameters.

## By-ref formals

A parameter declared with a leading `&` aliases the ref the caller
passes instead of receiving a copy of the value, so writing through it
with `set!` updates the caller:

```koskari
def add-a [a &result]:
  set! result (+ a 1)

var value 100
(add-a value &value)
value    ;; 101
```

`&result` in the formals marks the parameter as by-ref. `&value` at the
call is the ref reader shorthand; any ref-valued expression works there,
such as `(add-a value &(item xs 0))`. By-ref is allowed on positional and
keyword parameters only, never on the `*` or `**` parameters.

## Assignment macros

[`set!`](../reference/macros/compile-set.md) expands to the right special:

- Symbol assignment: `setq` (store)
- Augmented symbol assignment `(set! x (+ x 1))`: [`compile/inplace`](../reference/specials/compile-inplace.md)
- In-place item or attribute update: [`compile/inplace-item`](../reference/specials/compile-inplace-item.md) or [`compile/inplace-attr`](../reference/specials/compile-inplace-attr.md) via the `inplace` macro
- Plain item, attribute, or ref store: `set-item`, `set-attr`, or `set-ref`
- `(set! (values pattern ...) value)`: [`compile/setq-values`](../reference/specials/compile-setq-values.md)

## Unpack

Unpack splits one value across several names using a pattern: a list
shape with optional `*:` rest segments, the same family as `for-each`
bindings.

- Introduce names: [`compile/define-values`](../reference/specials/compile-define-values.md), [`let-values`](../reference/macros/compile-let-values.md)
- Store into existing names: [`compile/setq-values`](../reference/specials/compile-setq-values.md)

```koskari
var [x y] #(1 2)
set! (values a b) point
let-values [[u v] #(3 4)]:
  (+ u v)
```

The compiler evaluates the right-hand side once and assigns each binder.

## Vocabulary

| Term | One line |
|------|----------|
| bind | name to ref mapping |
| store | value through an existing ref |
| ref | handle to a mutable location |
| deref | read through a ref |
| resolve | what a symbol sees: this namespace, its parents, then builtins |
| unpack | one value to many names through a pattern |
