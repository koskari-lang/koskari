# Fibonacci

Five Fibonacci implementations in one file: direct recursion, memoization
through Python's `lru_cache`, a private cache captured with `@let`, tail
recursion through a named `let`, and an iterative `while` loop. The same
program is `examples/fib.ki` in the repository.

## The program

```koskari
import-from functools [lru_cache as: memoize]


def fib-recur [n]:
  if (< n 2):
    if (< n 1) 0 n
  else:
    + (fib-recur (- n 1)) (fib-recur (- n 2))


@(memoize maxsize: None)
@demand
def fib-recur-decorated [n]:
  if (< n 2):
    if (< n 1) 0 n
  else:
    + (fib-recur-decorated (- n 1)) (fib-recur-decorated (- n 2))


@let [[cache #{}]]
def fib-recur-cached [n]:
  if (in n cache):
    cache[n]
  elif (< n 2):
    if (< n 1) 0 n
  else:
    var result (+ (fib-recur-cached (- n 1))
                     (fib-recur-cached (- n 2)))
    set! cache[n] result
    result


def fib-tail-recur [n]:
  let inner [[n n] [v 0] [accu 1]]:
    if (< n 1):
      v
    else:
      inner (- n 1) accu (+ accu v)


def fib-loop [n]:
  var tmp 0
  var v 0
  var accu 1
  while (> n 0):
    decr! n
    set! tmp accu
    incr! accu v
    set! v tmp
  v


@entry-point
def main []:
  print "fib-recur:" (fib-recur 20)
  print "fib-recur-decorated:" (fib-recur-decorated 20)
  print "fib-recur-cached:" (fib-recur-cached 20)
  print "fib-tail-recur:" (fib-tail-recur 20)
  print "fib-loop:" (fib-loop 20)
```

`lru_cache` caches a returned value, so `fib-recur-decorated` stacks
`@demand` under it: without that the decorator would cache coroutines.
`@let` closes `fib-recur-cached` over one dict that outlives each call,
which a `var` inside the body would not.

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
| `fib-recur-decorated` | `@(memoize ...)` and `@demand` stacked on `def` |
| `fib-recur-cached` | `@let` over a `def`, `elif`, `set!` on a subscript |
| `fib-tail-recur` | Named `let` loop, tail calls |
| `fib-loop` | `while`, `decr!`, `incr!`, local `var` bindings |

## See also

- [layout/if](../../../reference/layouts/layout-if.md)
- [incr! / decr!](../../../reference/macros/compile-incr-and-compile-decr.md)
- [Decorators](../../../guide/decorators.md) (the `@let` graft)
- [Demand, async, and defer](../../../guide/demand-async-and-defer.md) (for `@demand`)
