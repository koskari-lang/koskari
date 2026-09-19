# compile/global and compile/local

**Syntax:** `(global NAME)`; `(local NAME)` or `(local NAME VALUE)`.

**Behaviour:** `global` expands to
[`define-global`](../specials/compile-define-global.md), so later stores
to `NAME` inside the function reach the module binding. `local` expands
to [`define`](../specials/compile-define.md), declaring a function-local
name, with an optional initial value. Under the default strict
resolution a store to a name bound in no enclosing scope is a compile
error, and these two forms are how a function says which scope it
means.

**Examples:**

```koskari
define server None

define start []:
  global server
  when (None? server):
    set! server (create-server)
  (server.serve)
```

**See also:** [Bindings and refs](../../guide/bindings-and-refs.md),
the `compiler/resolution` setting in
[Pragmas](../../guide/pragma.md).
