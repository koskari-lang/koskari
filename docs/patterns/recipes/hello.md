# Hello world

Run a minimal Koskari program.

## Prerequisites

- [Install and run](../../start/install-run.md)

## Steps

Create `hello.ki`:

```koskari
(print "Hello, Koskari!")
```

Run:

```bash
koskari hello.ki
```

## Variants

Simple layout line

```koskari
print "Hello from a line!"
```

Lines with more than one element are implicitly a call form.


Block layout

```koskari
print:
  "Hello from a block!"
```

The default layout transform collects each inner line as an
argument to the header.



## See also

- [Special forms: compile/call](../../reference/specials/compile-call.md)
- [Layout: layout/block](../../reference/layouts/layout-block-default.md)
