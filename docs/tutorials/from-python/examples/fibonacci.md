# Fibonacci

Five Fibonacci implementations in one file: direct recursion, memoization
through Python's `lru_cache`, a private cache captured with `@let`, tail
recursion through a named `let`, and an iterative `while` loop. The same
program is `examples/fib.ki` in the repository.

## Run

Save as `fib.ki` and:

```bash
koskari fib.ki
```

Expected:

```text
fib-recur: 6765
fib-recur-decorated: 6765
fib-recur-cached: 6765
fib-tail-recur: 6765
fib-loop: 6765
```

## What each one uses

| Name | Technique |
|------|-----------|
| `fib-recur` | Layout `if` / `else`, direct recursion |
| `fib-recur-decorated` | `@(memoize ...)` and `@demand` stacked on `define` |
| `fib-recur-cached` | `@let` over a `define`, `elif`, `set!` on a subscript |
| `fib-tail-recur` | Named `let` loop, tail calls |
| `fib-loop` | `while`, `decr!`, `incr!`, local `define` bindings |

## See also

- [layout/if](../../../reference/layouts/layout-if.md)
- [incr! / decr!](../../../reference/macros/compile-incr-and-compile-decr.md)
- [Decorators](../../../guide/decorators.md) (the `@let` graft)
- [Demand, async, and defer](../../../guide/demand-async-and-defer.md) (for `@demand`)
