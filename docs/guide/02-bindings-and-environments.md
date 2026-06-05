# Bindings and environments

A **binding** connects a name to a storage slot or value. An **environment** is
the place Koskari searches when it evaluates a name.

---

## Defining names

Use `define` to introduce a name:

```koskari
(define width 10)
(define height 4)
(* width height)
```

The last form evaluates to `40`. `define` creates the binding first, then stores
the value when an expression is provided.

A declaration can also reserve a name without assigning a value yet:

```koskari
(define total)
(setq total 0)
```

Reading a declaration-only name before it has a value is an error. That is
helpful: it catches “I forgot to set this” bugs early.

## Updating names

Use `setq` to store a new value in an existing binding:

```koskari
(define count 0)
(setq count (+ count 1))
(setq count (+ count 1))
count
```

`define` introduces; `setq` changes. Keeping that distinction makes code easier
to read.

## Local names with `let`

Use `let` when a name should exist only for a short body:

```koskari
(let [[width 10] [height 4]]
  (* width height))
```

The bindings `width` and `height` are available in the `let` body. Outside the
`let`, they do not leak into the surrounding code.

## Function parameters are bindings

Function parameters are local bindings created for each call:

```koskari
(define area [width height]
  (* width height))

(area 8 3)
```

Each call gets its own `width` and `height`. Changing a local name inside one
call does not change another call’s locals.

## Globals and locals

Most user code can rely on `define`, `setq`, `let`, and function parameters.
When you need to inspect environments directly, Koskari also exposes `globals`
and `locals` forms. Treat them as advanced tools for metaprogramming, debugging,
and interop, not as everyday program structure.

## What you should remember

- A binding gives a name a place to hold a value.
- `define` introduces a name; `setq` updates an existing name.
- `let` creates local names for a body.
- Function parameters are local names for one call.

Next: [Functions deep](03-functions-deep.md).
