# map and filter

Native lazy iteration builtins. **`map`** applies a function to each
element of a source; **`filter`** keeps the elements a predicate
approves. Both accept any iterable -- a sequence, a koskari generator,
a python generator, or a python async iterator -- and both can be
consumed synchronously or asynchronously.

Unlike their python counterparts, these run inside the koskari
evaluator: a koskari function passed as the mapper or predicate is
driven directly, with no wrapping, and may await.

## map

**Syntax:** `(map FUNC ITERABLE ...)`

Return a lazy iterator producing `(FUNC element)` for each element of
**`ITERABLE`**. Nothing is consumed until the map itself is iterated.

```koskari
(list (map (lambda [x] (* x 10)) #[1 2 3]))
; #[10 20 30]
```

With several iterables, **`FUNC`** takes one element from each per
step, in the order the iterables are written, and the map ends with
the shortest of them. The iterables are stepped left to right, so once
one is spent the ones after it are not asked for that step.

```koskari
(list (map + #[1 2 3] #[10 20 30]))
; #[11 22 33]

(list (map (lambda [a b] (#tuple a b)) #[1 2 3] "ab"))
; #[#(1 "a") #(2 "b")]
```

**`FUNC`** may be any callable: a koskari function, a python function,
or another callable object. A koskari function that suspends works
transparently when the map is consumed in an asynchronous context:

```koskari
import-from asyncio sleep

def slow-double [x]:
    (sleep 0)
    (* x 2)

for-each [v (map slow-double #[1 2 3])]:
    (print v)
```

A koskari function awaits a suspending call such as `sleep` on its own;
no `await` is written.

Matching python's `map`, a mapped function that raises
**`StopIteration`** truncates the iteration.

## filter

**Syntax:** `(filter PRED ITERABLE)`

Return a lazy iterator producing the elements of **`ITERABLE`** for
which `(PRED element)` is true. **`PRED`** may be **`None`**, keeping
the truthy elements -- the same convention as python's `filter`.

```koskari
(list (filter (lambda [x] (> x 1)) #[1 2 3]))
; #[2 3]

(list (filter None #[0 1 "" "x"]))
; #[1 "x"]
```

Like map, the predicate may be a koskari function and may await.

## Consuming a map or filter

Both are lazy single-pass iterators, usable from either surface:

- synchronously: `(list ...)`, `(next ...)`, `for-each`, or python
  `list()`, `for` and `next(iter(m))`. A map or filter is an iterable,
  not an iterator, so python `next()` needs the `iter()` step. A source
  or function that genuinely suspends on an awaitable cannot be driven
  this way and raises **`KoskariSyncViolation`**.
- asynchronously: `for-each` in an asynchronous context, or python
  `async for`. Awaits in the function, the predicate, or the source
  pass through transparently.

Exhaustion is permanent: a drained map or filter yields nothing on
later iteration. Closing one (python `aclose`, or generator close
semantics) closes its source.

## Type predicates

**`map?`** and **`filter?`** test for the native types:

```koskari
(map? (map str #[1]))          ; True
(filter? (py-filter None #[1])) ; False
```

## py-map and py-filter

The python builtins remain available as **`py-map`** and
**`py-filter`**, with **`py-map?`** and **`py-filter?`** as their type
predicates. See the python documentation for
[map](https://docs.python.org/3/library/functions.html#map) and
[filter](https://docs.python.org/3/library/functions.html#filter).

```koskari
(list (py-map str #[1 2]))   ; #["1" "2"]
(py-map? (py-map str #[1]))  ; True
```

The python builtins call their function through the python calling
convention. A koskari function used there behaves as it would in any
python higher-order function: wrap it with **`demand`** so each call
returns a completed value. The native **`map`** and **`filter`** need
no wrapping.
