# Runtime tuning

Two pragma-controlled mechanisms that change whether compiled code is
sealed read-only and how attribute access runs. Neither changes what a
program computes. Both default to the configuration that pays off for
ordinary modules.

## Attribute and method caching

Attribute access and method calls are the hot path of most Koskari code:
`self.total`, `box.a`, `(counter.bump v)`. Each of those, done naively,
walks the type's method-resolution order and probes a dictionary every
single time -- even when the same call site sees the same kind of object on
every pass through a loop. Koskari attaches a small per-site cache to
those instructions so that repeat is nearly free: the first time a site
runs it resolves the attribute the slow, fully-correct way and records how
it got there; every later run with the same receiver type replays that
answer behind a cheap guard, skipping the walk and the probe.

The cache is invisible to program behavior. It only ever makes a repeated
access faster; it never changes what an access returns or when it raises.
When the assumption behind a cached answer stops holding -- the receiver is
a different type, the class was monkeypatched, an instance grew or lost the
attribute -- the guard misses and the site falls back to the full
lookup.

### Monomorphic by design

Each site remembers one answer, not a table of them. A call site that
is used with a single receiver type, the common case, like
a method called on the same class each iteration -- stays on the fast path.
A site that is genuinely polymorphic, cycling through many receiver types,
does not grow an ever-larger cache: after enough guard misses it gives up
and runs uncached from then on, so a churning site costs a guard check at
worst, never a chain walk. This keeps every hit to a single comparison and
keeps the cache a fixed, small cost per site.

What it accelerates:

- Method calls -- `(obj.method args)` resolves the method once per site
  and, for plain Koskari and Python methods, invokes it without allocating a
  bound-method object.
- Attribute reads and writes -- `obj.attr` and `(set! obj.attr v)`
  resolve where the attribute lives once and then read or write that
  location directly. A `__slots__` member becomes a direct field access; a
  regular (dict-backed) instance attribute becomes a direct indexed read or
  write of the instance's inline values.

### The two pragmas

Caching is a compile-time choice per code object, governed by two module
pragmas that split on how often the code runs.

`compiler/cache-function` *(default on)* answers "cache inside function
and lambda bodies." These are the reused code that loops and repeated calls
flow through, so caching there pays for itself quickly. On is the baseline.

`compiler/cache-expression` *(default off)* answers "cache top-level
module and REPL expressions." Those typically run once as the module loads
or the line is entered, so a cache would never amortize; off is the
baseline. Turn it on only for a top-level body you know runs hot in a loop.

```
(pragma [compiler/cache-function False])     ; opt a module's functions out
(pragma [compiler/cache-expression True])    ; opt top-level code in
```

When caching is gated off for a code object, its instructions carry no
cache and run as they did before caching existed, correct and
unaccelerated.

### Python version caveats

The machinery works across the whole supported range (3.9 - 3.14), but one
part of it depends on how CPython stores instance attributes, which changed
mid-range:

- Method caching and `__slots__` attribute access work on every
  supported version.
- Direct caching of regular (dict-backed) instance attributes -- the
  `self.x` read and write fast path for ordinary classes -- requires the
  inline-value instance layout CPython introduced in 3.11. On **3.9 and
  3.10** those accesses stay correct but take the ordinary attribute path;
  they are not accelerated by the cache.

The floor is deliberate: Koskari runs on long-lived old interpreters
without denying them anything, and skips the newest fast path where
the runtime cannot support it. No source change is needed -- the same code
caches what it can on whichever interpreter it runs under.

### Builds without read-only code

A build without read-only code never seals anything. Emscripten builds
are always of this kind, because WebAssembly memory has no page
protection. Any other build can opt in by setting
`KOS_NO_PROTECT=1` in the environment when it compiles the extension.
`koskari.lib.build_flags['KOS_NO_PROTECT']` reports 1 in such a build and
0 otherwise.

In these builds:

- `compiler/pack-module` defaults to off.
- Turning `compiler/pack-module` or `compiler/protect-code` on raises
  `ValueError`, including through a bare declaration. Turning either off
  is accepted, so source that opts out runs everywhere.
- A `.kio` file loads normally whatever settings it was compiled under,
  and its code stays writable.
- `code.arena` is `None` and `code.protected` is `False` for all code.

### Introspection

- `code.caches` -- whether this code object was compiled with caching on
- `code.cache_slots` -- how many per-site cache slots it reserved

## Code sealing

Compiled Koskari code is immutable: once a function's bytecode is built,
nothing rewrites it. Sealing makes that immutability a property of the
memory. Sealed code is read-only, and a write to it faults on the spot
instead of silently corrupting the interpreter. That is the promise;
how code is stored, placed, shared or reclaimed is not part of it.

### Why read-only

Koskari's bytecode is interpreted, never executed as machine code, so
sealing is not required for correctness.

It does not stop an attacker who can already write anywhere in the
process; such an attacker has softer targets than a code stream. Nor
does it confine code running inside the interpreter: native access from
Python, through `ctypes` or an extension, is total. Sealing is not a
sandbox.

What it does is put code out of reach of the memory bugs that occur in
practice, which are relative, not arbitrary: a bounded overflow from a
neighbouring allocation, a stale pointer into storage since reused. A
write that reaches sealed code faults instead of taking effect.

It also doubles as a debugging tool. A buggy native extension that
scribbles over live code is caught at the offending write, with that
write's stack, instead of producing a wrong result later.

### The two pragmas

`compiler/pack-module` *(default on)* seals the code of every function
and lambda a module compiles while it loads, once the module has
finished loading. Until then that code is writable. Top-level
expressions -- the forms of the module body, each compiled, evaluated
once, and discarded -- are not covered by this setting.

`compiler/protect-code` *(default off)* seals each code object the
first setting leaves alone as it is built: top-level expressions, and
function code when `pack-module` is off.

Both pragmas depend on the system reporting its page size. Where it
does not, nothing is sealed. A build can also leave sealing out
altogether; see
[Builds without read-only code](#builds-without-read-only-code).

The four combinations:

| protect-code | pack-module | Functions | Top-level expressions |
|---|---|---|---|
| off | on *(default)* | sealed when the load completes | writable |
| off | off | writable | writable |
| on | on | sealed when the load completes | sealed from birth |
| on | off | sealed from birth | sealed from birth |

The default (off/on) is the right setting for ordinary modules: a
module's functions are read-only for their whole working life, and the
cost is paid once per module rather than once per code object.

Code compiled after a module has finished loading -- definitions
entered at the REPL, code compiled at runtime -- is sealed only under
`protect-code`.

### Precompiled modules

A `.kio` file records the `pack-module` setting in force when the
module was compiled, and the module loads with that setting. Its
functions are sealed before the first top-level form runs, so a
precompiled module's function code is read-only while its own body
runs, as it is for every module by the time its entry point runs.

Code decoded outside an import, through `koskari.serialize.load_kio`,
is not sealed.

### The cost of per-object sealing

Turning `compiler/protect-code` on makes every top-level expression's
bytecode read-only from the instant it is built. Function code under
the default `pack-module` is unaffected: it is writable until the load
completes either way.

What it costs, per code object:

- A full memory page each, however small the code, briefly, before the
  expression is discarded.
- A kernel mapping each. Large programs accrue thousands, approaching
  the system's limit on mappings per process, and every page fault
  pays for the larger set.
- Two system calls each: one to seal at construction, one to unseal
  before release.

None of this changes execution speed -- sealed and unsealed code read
identically -- but module loading gets slower and the process gets
bigger.

### Interactive sessions

Definitions entered at the REPL are not sealed -- interactive code
churns, and sealing churn buys little. The module a session starts
from (a file run with `-i`) is sealed normally when it finishes
loading, and the session keeps working in it afterward.

### Introspection

- `code.arena` -- the region the code shares with its module's other
  functions under `pack-module`, else `None`
- `arena.size`, `arena.used`, `arena.sealed` -- the region's size, its
  bytes in use, and whether it has been sealed
- `code.allocation_bytes` and friends -- the code object's storage
  footprint

## See also

- [Pragmas and feature settings](pragma.md): how to set the four
  settings, and the full catalog.
