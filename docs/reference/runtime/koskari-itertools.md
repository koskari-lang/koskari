# koskari.itertools

**Module:** `koskari.itertools`

**Imports:** `import-from koskari.itertools accumulate batched chain
chain-from-iterable compress cycle dropwhile filterfalse groupby islice
pairwise starmap takewhile zip-longest`

**Use-case:** The iterator combinators, in a form that accepts koskari
functions and koskari sources. Python's `itertools` does not: it calls
a predicate through the python calling convention, where a koskari
function returns a coroutine rather than an answer, and it pulls its
source through the synchronous iterator protocol, which an async
iterator does not answer to. Every combinator here takes a koskari
callable directly and accepts a sync iterable, an async iterable, or a
koskari generator as its source.

`map`, `filter` and `zip` are not in this module. They are bound bare
in the builtins namespace and already behave this way; python's remain
available as `py-map`, `py-filter` and `py-zip`.

Exhaustion is permanent: once a combinator reports its end, later
iteration yields nothing and its source is not read again, even when the
source would yield more. Python's `itertools` calls the source again.

## Why not python's itertools

Two things go wrong when a koskari function or a koskari source meets
`itertools`, and only one of them is loud.

A koskari callable handed to python's higher-order functions produces
a coroutine per call rather than a value. Nothing raises -- coroutines
are truthy, so a predicate reads as always true:

```koskari
(import-from itertools takewhile)

(list (takewhile (lambda [x] (< x 3)) #[1 2 3 4 1]))
; #[1 2 3 4 1] -- every element "passed"
```

```koskari
(import-from koskari.itertools takewhile)

(list (takewhile (lambda [x] (< x 3)) #[1 2 3 4 1]))
; #[1 2]
```

The first form is silently wrong: no error, plausible-looking output.
Wrapping the callable in `demand` restores the verdict, but forces
every call to run to completion, so the predicate can no longer
suspend.

The source is the other half. A koskari generator that never suspends
does reach python's `itertools`, through the demand adapter, which
forces each step to completion. A source that suspends does not: an
async iterator raises `TypeError` at construction, and a generator
that awaits cannot be stepped to completion at all.

These combinators drive their source through the evaluator, so a
suspending source works wherever the surrounding code can await it:

```koskari
import-from asyncio sleep
import-from koskari.itertools islice

def slow-numbers []:
  for-each [i #[1 2 3 4]]:
    (sleep 0)
    yield i

def first-two []:
  var out #[]
  for-each [x (islice (slow-numbers) 2)]:
    out.append x
  out
```

Note the consumer. `for-each` drives through the evaluator and awaits
in place; `list` is python's, so it drives synchronously and cannot
take a source that suspends -- that is a property of the consumer, not
of the combinator.

## accumulate

**Syntax:** `(accumulate ITERABLE)`,
`(accumulate ITERABLE FUNC)`, `(accumulate ITERABLE initial: SEED)`

Yield running totals. The first element is emitted as it stands and
becomes the first total; each later element is folded into it. With no
`FUNC` the fold is addition, so anything `+` accepts works. A `SEED`
is emitted before the source is read, so a seeded `accumulate` over an
empty source still yields once.

```koskari
(list (accumulate #[1 2 3 4]))
; #[1 3 6 10]

(list (accumulate #[1 2 3] initial: 10))
; #[10 11 13 16]
```

## batched

**Syntax:** `(batched ITERABLE N)`

Yield tuples of `N` consecutive elements. The final batch is short
when the source does not divide evenly. `N` must be at least one.

```koskari
(list (batched #[0 1 2 3 4 5 6] 3))
; #[#(0 1 2) #(3 4 5) #(6)]
```

## chain

**Syntax:** `(chain ITERABLE ...)`, `(chain-from-iterable ITERABLES)`

Yield every element of each source in turn. A source is acquired only
when the one before it runs out, so a source never reached is never
touched -- which matters when acquiring one has effects, such as
calling a generator function or opening a file in `__iter__`.

`chain-from-iterable` takes the series from a single iterable rather
than from the argument list.

```koskari
(list (chain #[1 2] "ab"))
; #[1 2 "a" "b"]
```

## compress

**Syntax:** `(compress DATA SELECTORS)`

Yield the elements of `DATA` whose matching selector is truthy. Ends
when either source runs out.

```koskari
(list (compress "ABCDEF" #[1 0 1 0 1 1]))
; #["A" "C" "E" "F"]
```

## cycle

**Syntax:** `(cycle ITERABLE)`

Repeat the source endlessly. The first pass keeps a copy of each
element as it yields it, and every later lap reads that copy, so the
source is walked exactly once. An empty source yields nothing rather
than looping forever.

```koskari
(list (islice (cycle "ABC") 8))
; #["A" "B" "C" "A" "B" "C" "A" "B"]
```

## dropwhile

**Syntax:** `(dropwhile PRED ITERABLE)`

Drop elements while `PRED` holds, then yield the rest. The predicate
is retired after its first rejection and never consulted again, so
later elements that would satisfy it are still yielded.

```koskari
(list (dropwhile (lambda [x] (< x 3)) #[1 2 3 4 1 2]))
; #[3 4 1 2]
```

## filterfalse

**Syntax:** `(filterfalse PRED ITERABLE)`

Yield the elements `filter` would drop. A `None` predicate keeps the
falsey elements. The result is a `filter` carrying an inverted
verdict, so `filter?` answers true for it.

```koskari
(list (filterfalse (lambda [x] (% x 2)) #[1 2 3 4]))
; #[2 4]
```

## groupby

**Syntax:** `(groupby ITERABLE)`, `(groupby ITERABLE KEY)`

Yield `#(key group)` for each run of consecutive elements sharing a
key, where `KEY` defaults to the element itself. Non-adjacent runs of
the same key are separate groups.

The group is a list, not a view onto the source. Python's `groupby`
hands back a sub-iterator sharing one cursor with its parent, valid
only until the parent advances; here each run is materialized, so a
group stays usable for as long as you hold it.

```koskari
(list (groupby "AAABBBCCD"))
; #[#("A" #["A" "A" "A"]) #("B" #["B" "B" "B"])
;   #("C" #["C" "C"]) #("D" #["D"])]
```

## islice

**Syntax:** `(islice ITERABLE STOP)`,
`(islice ITERABLE START STOP)`, `(islice ITERABLE START STOP STEP)`

Yield a slice of the source by position. Bounds count forward only:
`START` and `STOP` must be non-negative, `STEP` at least one, and
`STOP` may be `None` for an open end. Skipped elements are consumed,
and a spent slice leaves the rest of the source unread.

```koskari
(list (islice #[0 1 2 3 4 5 6 7 8 9] 2 7 2))
; #[2 4 6]
```

## pairwise

**Syntax:** `(pairwise ITERABLE)`

Yield overlapping pairs of consecutive elements. A source of fewer
than two elements yields nothing.

```koskari
(list (pairwise "ABCD"))
; #[#("A" "B") #("B" "C") #("C" "D")]
```

## starmap

**Syntax:** `(starmap FUNC ITERABLE)`

Call `FUNC` with each element unpacked as its arguments, rather than
as a single argument. Each element must be iterable.

```koskari
(list (starmap (lambda [a b] (+ a b)) #[#(1 2) #(3 4)]))
; #[3 7]
```

## takewhile

**Syntax:** `(takewhile PRED ITERABLE)`

Yield elements while `PRED` holds and stop at the first that fails.
That element is consumed and dropped; the source is left open, so
whatever holds it can carry on from the element after.

```koskari
(list (takewhile (lambda [x] (< x 3)) #[1 2 3 4 1]))
; #[1 2]
```

## zip-longest

**Syntax:** `(zip-longest ITERABLE ...)`,
`(zip-longest ITERABLE ... fillvalue: FILL)`

Pair elements positionally, continuing until every source is spent
rather than stopping at the first. A spent source contributes `FILL`,
which defaults to `None`.

```koskari
(list (zip-longest #[1 2 3] "ab"))
; #[#(1 "a") #(2 "b") #(3 None)]

(list (zip-longest #[1 2 3] "ab" fillvalue: 0))
; #[#(1 "a") #(2 "b") #(3 0)]
```

## See also

- [map and filter](map-and-filter.md) for the two that are bare in the
  builtins namespace, and their `py-` counterparts
- [koskari.solve](koskari-solve.md) for another module built on the
  evaluator's own machinery
