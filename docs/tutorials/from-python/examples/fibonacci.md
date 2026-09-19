# Fibonacci

Five Fibonacci implementations in one file: direct recursion, memoization
through Python's `lru_cache`, a private cache captured with `@let`, tail
recursion through a named `let`, and an iterative `while` loop. The same
program is `examples/fib.ki` in the repository.

## Prerequisites

- [Quickstart](../../../quickstart.md)
- [From Python: Functions and scope](../functions-and-scope.md)
- [From Python: Data and control](../data-and-control.md)

## Program

```koskari
import-from functools [lru_cache as: memoize]


define fib-recur [n]:
    """
    Fibonacci, defined recursively
    """

    if (< n 2):
        if (< n 1) 0 n
    else:
        + (fib-recur (- n 1)) (fib-recur (- n 2))


;; lru_cache is a Python decorator, so the function it wraps has to
;; produce a value rather than a coroutine. @demand puts it on the
;; demand path.
@(memoize maxsize: None)
@demand
define fib-recur-decorated [n]:
    """
    Fibonacci, defined recursively, memoized by lru_cache
    """

    if (< n 2):
        if (< n 1) 0 n
    else:
        + (fib-recur-decorated (- n 1)) (fib-recur-decorated (- n 2))


;; @let closes the function over a private cache, so the memo
;; persists across calls. A cache defined inside the body would be
;; rebuilt on every call.
@let [[cache #{}]]
define fib-recur-cached [n]:
    """
    Fibonacci, defined recursively with a cache
    """

    if (in n cache):
        cache[n]

    elif (< n 2):
        if (< n 1) 0 n

    else:
        define result (+ (fib-recur-cached (- n 1))
                         (fib-recur-cached (- n 2)))
        set! cache[n] result
        result


define fib-tail-recur [n]:
    """
    Fibonacci, defined with a tail-recursive named let
    """

    let inner [[n n] [v 0] [accu 1]]:
        if (< n 1):
            v
        else:
            inner (- n 1) accu (+ accu v)


define fib-loop [n]:
    """
    Fibonacci, defined iteratively
    """

    define tmp 0
    define v 0
    define accu 1

    while (> n 0):
        decr! n
        set! tmp accu
        incr! accu v
        set! v tmp
    v


when-main:
    print "fib-recur:" (fib-recur 20)
    print "fib-recur-decorated:" (fib-recur-decorated 20)
    print "fib-recur-cached:" (fib-recur-cached 20)
    print "fib-tail-recur:" (fib-tail-recur 20)
    print "fib-loop:" (fib-loop 20)
```

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
