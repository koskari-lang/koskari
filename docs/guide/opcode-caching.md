# Attribute and method caching

Attribute access and method calls are the hot path of most Koskari code:
`self.total`, `box.a`, `(counter.bump v)`. Each of those, done naively,
walks the type's method-resolution order and probes a dictionary every
single time -- even when the same call site sees the same kind of object on
every pass through a loop. Koskari attaches a small **per-site cache** to
those instructions so that repeat is nearly free: the first time a site
runs it resolves the attribute the slow, fully-correct way and records how
it got there; every later run with the same receiver type replays that
answer behind a cheap guard, skipping the walk and the probe.

The cache is invisible to program behavior. It only ever makes a repeated
access faster; it never changes what an access returns or when it raises.
When the assumption behind a cached answer stops holding -- the receiver is
a different type, the class was monkeypatched, an instance grew or lost the
attribute -- the guard misses and the site quietly falls back to the full
lookup.

## Monomorphic by design

Each site remembers **one** answer, not a table of them. A call site that
is used with a single receiver type -- the overwhelmingly common case, like
a method called on the same class each iteration -- stays on the fast path.
A site that is genuinely *polymorphic*, cycling through many receiver types,
does not grow an ever-larger cache: after enough guard misses it gives up
and runs uncached from then on, so a churning site costs a guard check at
worst, never a chain walk. This keeps every hit to a single comparison and
keeps the cache a fixed, small cost per site.

What it accelerates:

- **Method calls** -- `(obj.method args)` resolves the method once per site
  and, for plain Koskari and Python methods, invokes it without allocating a
  bound-method object.
- **Attribute reads and writes** -- `obj.attr` and `(set! obj.attr v)`
  resolve *where the attribute lives* once and then read or write that
  location directly. A `__slots__` member becomes a direct field access; a
  regular (dict-backed) instance attribute becomes a direct indexed read or
  write of the instance's inline values.

## The two pragmas

Caching is a compile-time choice per code object, governed by two module
pragmas that split on how often the code runs.

**`compiler/cache-function`** *(default on)* answers "cache inside function
and lambda bodies." These are the reused code that loops and repeated calls
flow through, so caching there pays for itself quickly. On is the baseline.

**`compiler/cache-expression`** *(default off)* answers "cache top-level
module and REPL expressions." Those typically run once as the module loads
or the line is entered, so a cache would never amortize; off is the
baseline. Turn it on only for a top-level body you know runs hot in a loop.

```
(pragma [compiler/cache-function False])     ; opt a module's functions out
(pragma [compiler/cache-expression True])    ; opt top-level code in
```

When caching is gated off for a code object, its instructions carry no
cache and run exactly as they did before caching existed -- correct, just
unaccelerated.

## Python version caveats

The machinery works across the whole supported range (3.9 - 3.14), but one
part of it depends on how CPython stores instance attributes, which changed
mid-range:

- **Method caching and `__slots__` attribute access** work on **every**
  supported version.
- **Direct caching of regular (dict-backed) instance attributes** -- the
  `self.x` read and write fast path for ordinary classes -- requires the
  inline-value instance layout CPython introduced in **3.11**. On **3.9 and
  3.10** those accesses stay correct but take the ordinary attribute path;
  they are not accelerated by the cache.

This is a deliberate floor: Koskari runs on long-lived old interpreters
without denying them anything, and simply skips the newest fast path where
the runtime cannot support it. No source change is needed -- the same code
caches what it can on whichever interpreter it runs under.

## Introspection

- `code.caches` -- whether this code object was compiled with caching on
- `code.cache_slots` -- how many per-site cache slots it reserved

## See also

- [Pragmas and feature settings](pragma.md) -- how to set the two settings,
  and the full catalog.
