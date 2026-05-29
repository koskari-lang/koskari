# Hello world

Run a minimal Koskari program.

## Prerequisites

- [Install and run](../../start/install-run.md)

## Steps

Create `hello.ki`:

```scheme
(print "Hello, Koskari!")
```

Run:

```bash
koskari hello.ki
```

## Variants

Layout style:

```scheme
print:
  "Hello from a block!"
```

## See also

- [Special forms: compile/call](../../reference/specials/compile-call.md)
- [Layout: layout/call](../../reference/layouts/layout-call.md)
