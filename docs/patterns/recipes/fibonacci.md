# Fibonacci

Several Fibonacci implementations in one file: recursive layout `if`,
memoization, manual cache, tail recursion, and an iterative `while` loop with
`decr!`.

## Prerequisites

- [Install and run](../../start/install-run.md)
- [From Python: Functions and scope](../../start/from-python/functions-and-scope.md)
- [From Python: Data and control](../../start/from-python/data-and-control.md)

## Source

Repository file: [examples/fib.ki](../../../examples/fib.ki)

## Run

```bash
koskari examples/fib.ki
```

The file defines functions but does not print by default. Try in the REPL after
loading, or add a short `when-main:` block:

```koskari
when-main:
  print (fib-loop 10)
```

## Highlights

| Name | Technique |
|------|-----------|
| `fib-recur` | Layout `if` / `else`, direct recursion |
| `fib-recur-decorated` | `@memoize` and `@demand` on `define` |
| `fib-recur-cached` | `let`, `elif`, dict cache, `set!` on `item` |
| `fib-tail-recur` | Named `let` loop, tail calls |
| `fib-loop` | `while`, `decr!`, local `define` bindings |

## See also

- [layout/if](../../reference/layouts/layout-if.md)
- [incr! / decr!](../../reference/macros/compile-incr-and-compile-decr.md)
- [Demand, async, and defer](../../reference/concepts/demand-async-and-defer.md) (for `@demand`)
