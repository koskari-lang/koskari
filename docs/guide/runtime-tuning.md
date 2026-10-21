# Runtime tuning

Two pragma-controlled mechanisms that change how compiled code is stored
and how attribute access runs. Neither changes what a program computes.
Both default to the configuration that pays off for ordinary modules.

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

### Introspection

- `code.caches` -- whether this code object was compiled with caching on
- `code.cache_slots` -- how many per-site cache slots it reserved

## Code sealing and arena packing

Compiled Koskari code is immutable: once a function's bytecode is built,
nothing rewrites it. Sealing makes that immutability real at the
operating-system level -- the memory holding the instructions, constant
pool, and control tables is marked read-only with `mprotect`, so any
write to it faults on the spot instead of silently corrupting the
interpreter. Koskari's bytecode is interpreted, never executed as
machine code, so sealing is not required for correctness; it is
integrity hardening, and it doubles as a debugging tool -- a buggy
native extension that scribbles over live code is caught at the exact
offending write.

Two module pragmas govern the model.

### The two pragmas

`compiler/pack-module` *(default on)* places function code in arenas.
While a module loads, the code of every function and lambda it
compiles is written directly into the load's arena chunks, and stays
at that address for its whole life. When the module finishes loading,
every chunk is sealed read-only with one `mprotect` each. Top-level
expressions -- the forms of the module body, each compiled, evaluated
once, and discarded -- are never placed in an arena.

`compiler/protect-code` *(default off)* seals each code object that is
not placed in an arena as it is built: top-level expressions, and
function code when `pack-module` is off. On, each such code object gets
its own page-aligned, sealed allocation the moment it exists. Off, it
is built on the plain heap.

The four combinations:

| protect-code | pack-module | Functions | Top-level expressions |
|---|---|---|---|
| off | on *(default)* | arena chunks, sealed when the load completes | plain heap |
| off | off | plain heap, never sealed | plain heap |
| on | on | arena chunks, sealed when the load completes | own sealed page each |
| on | off | own sealed page each, from birth | own sealed page each |

The default (off/on) is the right setting for ordinary modules: after
loading, a module's functions sit nearly back-to-back in a few
read-only pages instead of each occupying its own.

### Arena chunks

A chunk is one page (on systems whose pages are larger than 4 KB, one
of their pages). Each function's code goes into the first chunk with
room for it; when no chunk has room, a new one is mapped, and a
function larger than a page gets a chunk of its own, rounded up to
whole pages.

A chunk lives until the last code object inside it dies, so a single
survivor (a returned closure, a function cached in a registry, a
suspended coroutine over a module function) keeps its chunk resident,
and no more.

Function code that dies before its module finishes loading -- a lambda
built by a top-level expression and then dropped, or a definition a
later one replaced -- leaves its bytes in its chunk, unless it was the
newest code in that chunk, in which case the next function reuses the
space.

Code compiled after the load completes is plain heap: definitions
entered at the REPL, and code compiled at runtime.

### Precompiled modules

A `.kio` file records, for each function, whether its build placed it
in an arena, so the `pack-module` setting in force when the module was
compiled is the one it loads with. Importing the `.kio` builds every
function before the first top-level form runs and seals the chunks at
that moment. A precompiled module's code is read-only while its own
body runs, including a `when-main` body that never returns. A function
that only a discarded top-level expression held keeps its bytes in its
chunk.

### The cost of per-object sealing

Turning `compiler/protect-code` on makes every top-level expression's
bytecode read-only from the instant it is built. Function code under
the default `pack-module` is unaffected: it is writable until the load
completes either way.

What it costs, per code object:

- A full memory page each. Protection is page-granular, so a
  fifty-byte expression occupies (typically) 4 KB, briefly, before it is
  discarded.
- A kernel mapping each. `mprotect` on heap memory splits the
  process's virtual memory areas, so every sealed object becomes a
  distinct kernel VMA. Large programs accrue thousands, approaching
  the `vm.max_map_count` limit, and every page fault pays for a bigger
  VMA tree.
- Two syscalls each. One `mprotect` to seal at construction, one to
  unseal before free.

None of this changes execution speed -- sealed and unsealed code read
identically -- but module loading gets slower and the process gets
fatter.

### Interactive sessions

Definitions entered at the REPL are cheap plain-heap allocations --
interactive code churns, and sealing churn buys little. The module a
session starts from (a file run with `-i`) is sealed normally when it
finishes loading, and the session keeps working in it afterward.

### Introspection

- `code.arena` -- the chunk holding the code, else `None`
- `arena.size`, `arena.used`, `arena.sealed` -- the chunk's size, the
  end of its newest code, and whether the seal has been applied
- `code.allocation_bytes` and friends -- the code object's storage
  footprint

## See also

- [Pragmas and feature settings](pragma.md): how to set the four
  settings, and the full catalog.
