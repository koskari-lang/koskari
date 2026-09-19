# Runtime tuning

Two pragma-controlled mechanisms that change how compiled code is stored
and how attribute access runs. Neither changes what a program computes.
Both default to the configuration that pays off for ordinary modules.

## Attribute and method caching

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

### Monomorphic by design

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

### The two pragmas

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

### Python version caveats

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

### Introspection

- `code.caches` -- whether this code object was compiled with caching on
- `code.cache_slots` -- how many per-site cache slots it reserved

## Code sealing and arena packing

Compiled Koskari code is immutable: once a function's bytecode is built,
nothing rewrites it. *Sealing* makes that immutability real at the
operating-system level — the memory holding the instructions, constant
pool, and control tables is marked read-only with `mprotect`, so any
write to it faults on the spot instead of silently corrupting the
interpreter. Koskari's bytecode is interpreted, never executed as
machine code, so sealing is not required for correctness; it is
integrity hardening, and it doubles as a debugging tool — a buggy
native extension that scribbles over live code is caught at the exact
offending write.

Two module pragmas govern the model. They answer different questions
and compose freely.

### The two pragmas

**`compiler/protect-code`** *(default off)* answers "seal each code
object as it is born." On, every compiled code object — including each
top-level expression that is evaluated once and discarded — gets its
own page-aligned, sealed allocation the moment it exists. Off, code is
built on the plain heap.

**`compiler/pack-module`** *(default on)* answers "compact what the
module keeps." When a module finishes loading, its surviving code
objects — the functions and classes that made it into the module's
namespace — are relocated together into one shared allocation (an
*arena*) and sealed read-only with a single `mprotect`. What survives
is decided by reachability: imported functions belong to their own
module's arena, and a top-level expression that was evaluated and
dropped is already gone by the time the module seals.

The four combinations:

| protect-code | pack-module | Result |
|---|---|---|
| off | on *(default)* | code is writable heap during load, then the module's durable code shares one sealed arena — compact, read-only at rest |
| off | off | plain heap for everything; lightest, never protected |
| on | on | each object protected from birth, then repacked into a sealed arena — protected during load *and* at rest, at the cost of a higher transient peak |
| on | off | every object holds its own sealed page for life |

The default (off/on) is the sweet spot for ordinary modules: after
loading, a module's functions sit nearly back-to-back in one or two
read-only pages instead of each occupying its own.

### Tuning the arenas

`pragma [compiler/pack-module]` accepts two options; `0` (the default
for both) means unbounded, producing one arena per module:

```
(pragma [compiler/pack-module True max-count: 32])
(pragma [compiler/pack-module True max-bytes: 65536])
```

`max-count:` caps how many code objects share one arena; `max-bytes:`
is a soft cap on an arena's payload — a single code object larger than
the cap still lands in its own arena rather than failing.

The reason to bound arenas is **lifetime pinning**: an arena lives
until the *last* code object inside it dies. For a module whose
functions load and unload together — the overwhelmingly common case —
that is exactly right. But a single survivor (a returned closure, a
function cached in a registry, a suspended coroutine over a module
function) keeps its entire arena resident. Capping arena size caps the
blast radius of any one survivor. Keep the unbounded default unless you
harvest a few functions from large, otherwise-transient modules.

### The cost of per-expression sealing

Turning `compiler/protect-code` on buys during-load protection: a
function's bytecode is read-only from the instant it is defined, so
nothing that runs later in the module body — buggy or hostile — can
alter an already-defined function before the module seals. Whether that
window matters is a threat-model question, and it is the *only* thing
on offer, because at rest the on/on and off/on configurations end up
identical.

What it costs, per code object:

- **A full memory page each.** Protection is page-granular, so a
  fifty-byte expression occupies (typically) 4 KB. Every top-level
  expression in the module body — each one compiled, evaluated once,
  and discarded — briefly burns a page of its own.
- **A kernel mapping each.** `mprotect` on heap memory splits the
  process's virtual memory areas, so every sealed object becomes a
  distinct kernel VMA. Large programs accrue thousands, approaching
  the `vm.max_map_count` limit, and every page fault pays for a bigger
  VMA tree.
- **Two syscalls each.** One `mprotect` to seal at construction, one to
  unseal before free.
- **A higher transient peak with packing.** Under on/on, the per-page
  originals and the arena coexist during the repack before the
  originals are released.

None of this changes execution speed — sealed and unsealed code read
identically — but module loading gets slower and the process gets
fatter. Reach for `[compiler/protect-code]` when the during-load window
is part of your threat model; leave it off otherwise and let the
default packing give you sealed code at rest for almost nothing.

### Interactive sessions

Definitions entered at the REPL are cheap plain-heap allocations —
interactive code churns, and sealing churn buys little. The module a
session starts from (a file run with `-i`) is sealed normally when it
finishes loading, and the session keeps working in it afterward.

### Introspection

- `code.arena` — the owning arena when packed, else `None`
- `arena.size`, `arena.used`, `arena.sealed` — total allocation,
  bytes occupied, and whether the seal has been applied
- `code.allocation_bytes` and friends — the code object's storage
  footprint

## See also

- [Pragmas and feature settings](pragma.md): how to set the four
  settings, and the full catalog.
