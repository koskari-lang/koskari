# Bindings and refs

Koskari names do not hold values directly. Each binding in a **namespace**
maps a name to a **ref** (reference cell). Reading and writing go through that
cell.

## Namespace

A **namespace** is a lexical scope: a table of names, each tied to a ref.
Modules, `let` bodies, and function frames each use namespaces (often chained
to a parent). See the [Glossary](../reference/glossary.md) entry for **namespace**.

## Three tiers of a namespace

A namespace answers questions at three tiers, and its Python-facing API keeps
them apart.

| Tier | What it holds | Reached through |
|------|---------------|-----------------|
| 1 | builtins: the names a module falls through to | `ns.builtins`, a read-only mapping (module namespaces only) |
| 2 | refs: the names bound here, each to a ref | `ns.refs`, `ns.ref(name)`, `ns.bind(name, ref)`, `ns.bound(name)` |
| 3 | values: what those refs currently hold | the mapping protocol: `name in ns`, `ns[name]`, `ns.get(name)`, `len(ns)`, `keys()`, `values()`, `items()`, iteration; also `ns.deref(name)` |

Tier 2 is append-only. A name once bound is never unbound, only emptied,
because closures may already hold its ref. `del ns[name]` and `ns.clear(name)`
empty the ref; afterwards `name in ns` is false while `name in ns.refs` stays
true. `ns.ref(name)` on a module namespace binds a fresh ref when the name is
missing, so a request for a ref is the one read that creates. The new ref
starts out holding the builtin of that name when there is one, and empty
otherwise. That copy is the module's own: storing through it changes the name
inside this module only, and later changes to the builtins are not seen
through it.

The mapping protocol never reaches tier 1. A builtin is not `in` a module
namespace, `ns[name]` raises `KeyError` for it, and `ns.get(name)` returns the
default. The lookup a symbol performs when evaluated, this namespace's value,
then each enclosing namespace's, then the module's builtins, is
`ns.resolve(name)`, with an optional default in place of `NameError`.

A `Module` object's attributes are tier 3 of its namespace: `module.name`
reads a value the module holds and `dir(module)` lists those names. Builtins
are not attributes of a module.

## Ref

A **ref** is a mutable handle: `deref` reads the current value, `store` writes,
`clear` empties the slot. Refs can represent a local name, an attribute, a
subscript, a parameter, or other custom get/set logic.

Surface forms:

- **`(ref sym)`** — ref to a variable ([`compile/ref`](../reference/macros/compile-ref.md))
- Reader **`&name`** / **`&obj.attr`** — an atom prefixed with `&` wraps it in `(ref ...)`
- Reader **`&(item coll key)`** — `&` immediately followed by `(` wraps the parenthesized form in `(ref ...)`
- **`(ref (attr obj name))`**, **`(ref (item coll key))`**, parameters, etc.

Bytecode **`VAR_REF`** pushes the ref for a name; attribute and item refs use
their own opcodes.

## Bind vs store

These words describe different operations. They are not interchangeable.

| Operation | Meaning | Typical compile form |
|-----------|---------|----------------------|
| **Bind** | Attach a name to a ref (introduce or replace the mapping) | [`compile/bindq`](../reference/specials/compile-bindq.md), [`bind!`](../reference/macros/compile-bind.md) |
| **Store** | Write a value through the ref a name already uses | [`compile/setq`](../reference/specials/compile-setq.md), `set!` |

- **Bind** answers: “what ref does this name resolve to?”
- **Store** answers: “what value lives in that ref right now?”

You must **bind** before you can **store** into a new local. Reassignment of an
existing variable is **store**, not a new bind.

The [`bind!`](../reference/macros/compile-bind.md) macro can install typed refs (item,
attribute, dynamic, parameter, environ, and others) while still using bind under
the hood. The **environ** ref reads and writes a process environment variable
through `os.environ`; see
[`compile/define-environ`](../reference/macros/compile-define-environ.md).

## Deref

**Deref** means read through a ref and use the value.

- **`(deref expr)`** — [`compile/deref`](../reference/specials/compile-deref.md)
- Runtime **`deref`** builtin — when you need `deref` as a first-class function

Use **`ref`** when you need the cell (for example passing a mutable slot to
another form). Use **deref** when you need the contents.

## By-ref formals

A function parameter can be declared **by-ref** by prefixing its name with `&`
in the formals list. Instead of receiving a copy of the argument's value, a
by-ref parameter aliases the **ref** the caller passes in: the callee's local
shares storage with the caller's binding, so writing through it with `set!`
updates the caller.

```koskari
(define add-a [a &result]
  (set! result (+ a 1)))

(define value 100)
(add-a value &value)
; value is now 101
```

Two distinct uses of `&`:

- `&result` in the **formals list** marks the parameter as by-ref.
- `&value` at the **call site** is the ref reader shorthand (see the **Ref**
  section above) — it passes the caller's ref so the alias has something to bind
  to. You can equally pass any ref-valued expression, e.g.
  `(add-a value &(item xs 0))`.

By-ref is allowed on **positional and keyword** parameters only — never on the
`*` (rest) or `**` (keyword-rest) parameters.

## Assignment macros

[`set!`](../reference/macros/compile-set.md) expands to the right special:

- Symbol assignment → `setq` (store)
- Augmented symbol assignment **`(set! x (+ x 1))`** → [`compile/inplace`](../reference/specials/compile-inplace.md)
- In-place item or attribute update → [`compile/inplace-item`](../reference/specials/compile-inplace-item.md) or [`compile/inplace-attr`](../reference/specials/compile-inplace-attr.md) via the **`inplace`** macro
- Plain item, attribute, or ref store → `set-item`, `set-attr`, or `set-ref`
- **`(set! (values pattern ...) value)`** → [`compile/setq-values`](../reference/specials/compile-setq-values.md)

## Unpack

**Unpack** splits one value across several names using a **pattern** (list
shape, optional `*:` rest segments). Same pattern family as `for-each` bindings.

- Introduce names: [`compile/define-values`](../reference/specials/compile-define-values.md),
  [`let-values`](../reference/macros/compile-let-values.md)
- Store into existing names: [`compile/setq-values`](../reference/specials/compile-setq-values.md)

```koskari
(define-values [x y] (pair 1 2))
(set! (values a b) point)
(let-values [[u v] pair] (+ u v))
```

The compiler emits unpack opcodes that evaluate the right-hand side once, then
assign each binder.

## Quick reference

| Term | One line |
|------|----------|
| bind | Name → ref mapping |
| store | Value → through existing ref |
| ref | Handle to a mutable location |
| deref | Read through a ref |
| resolve | What a symbol sees: this namespace, its parents, then builtins |
| unpack | One value → many names via pattern |
