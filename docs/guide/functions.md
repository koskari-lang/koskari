# Functions

Definition, formals, results, closures, recursion, and the two spellings
of an anonymous function.

## Defining

The layout form and the parenthesized form are the same definition:

```koskari
define double [n]:
  (* n 2)

(define double [n]
  (* n 2))
```

A body that fits on the header line needs no colon:

```koskari
define double [n] (* n 2)
```

`define` with formals is sugar for binding a `function` value to a
name. `lambda` makes the same value without a name:

```koskari
define add-one (lambda [x] (+ x 1))
(add-one 41)
```

Functions are values. Pass them, store them, return them.

## Formals

The formals list holds positional names, keyword names with defaults,
and the two rest collectors:

```koskari
define connect [host port: 80 *: extra **: options]:
  #(host port extra options)

(connect "a" 8080 1 2 verbose: True)
;; ("a", 8080, (1, 2), {'verbose': True})
```

- `name` is positional and required.
- `name: default` is a keyword with a default, passed as `name: value`.
- `*: rest` collects extra positionals into a tuple.
- `**: options` collects extra keywords into a dict.

A `&name` formal is by reference: it aliases the caller's storage
instead of copying the value, so `set!` inside the function writes back
to the caller. It has its own page,
[References and storage indirection](references-and-storage-indirection.md).

Annotations attach with `::` and land in `__annotations__` as real
objects; see [Types and annotations](types-and-annotations.md).

## Results

A body runs in order and its last form is the result:

```koskari
define shipping [subtotal]:
  if (>= subtotal 50):
    0
  else:
    5
```

`return` leaves early. It is a call, so `(return)` and `(return x)` are
the spellings, and a bare `return` on a layout line expands to
`(return)`:

```koskari
define first-even [xs]:
  for-each [x xs]:
    when (== 0 (% x 2)):
      return x
  None
```

`return` is variadic. Two or more positional arguments return a tuple;
keyword arguments return a `values` object:

```koskari
define minmax [a b]:
  if (< a b):
    return a b
  else:
    return b a

define-values [lo hi] (minmax 8 3)   ;; lo = 3, hi = 8

define stats [xs]:
  return count: (len xs) first: xs[0]

(stats #[10 20 30])   ;; values(count=3, first=10)
```

Positional and keyword arguments cannot be mixed in one `return`.
Destructure a tuple result with
[`define-values`](../reference/specials/compile-define-values.md) or
[`let-values`](../reference/macros/compile-let-values.md).

## Closures

A function closes over the namespace it was defined in. An inner
function reads outer names, and `set!` inside it stores into the outer
name it finds, with no declaration:

```koskari
define make-counter []:
  define count 0
  define tick []:
    incr! count
    count
  tick

define t (make-counter)
(t)
(t)   ;; 2
```

The `global` and `local` macros exist to say explicitly which scope a
store should reach; see
[`global` and `local`](../reference/macros/compile-global-and-compile-local.md).

## Recursion and named `let`

A recursive function calls itself. Write the stopping case first:

```koskari
define countdown [n]:
  if (<= n 0):
    print "done"
  else:
    print n
    (countdown (- n 1))
```

Tail calls do not grow the stack, so a loop written as recursion costs
what a `while` costs. The named `let` is the idiom for that:

```koskari
define fib [n]:
  let loop [[n n] [a 0] [b 1]]:
    if (< n 1):
      a
    else:
      (loop (- n 1) b (+ a b))
```

`loop` is bound inside the body to a function of the same bindings, and
calling it is the next iteration.

## Decorators

An `@` line above a `define` applies a decorator, as in Python. Koskari
adds decorator forms that wrap the definition syntactically, such as
`@let` closing a function over a private binding. The whole surface is
in [Decorators](decorators.md).

## Calling from Python

A Koskari function called from Koskari yields a value. Called from
plain Python code, with no event loop, it yields a coroutine, because
every Koskari function can suspend. `define-demanding`, or `@demand`
over a `define`, gives Python a function that returns the value. That
applies to any function a Python caller invokes directly, including a
Python decorator like `lru_cache` that calls what it wraps. The model
is in [Demand, async, and defer](demand-async-and-defer.md).

## Thunks

Braces make a short anonymous function whose formals are inferred from
`%1`, `%2`, and so on:

```koskari
(list (map {(* %1 %1)} #[1 2 3]))   ;; [1, 4, 9]
```

`{(* %1 %1)}` is `(lambda [%1] (* %1 %1))`. Use it for a one-expression
callback; name anything longer.
