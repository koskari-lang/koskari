# Profile-Guided Optimization for the Native Build

The C extension in `koskari/lib` is a computed-goto bytecode interpreter: a
near-ideal target for profile-guided optimization (PGO). The hot path is
data-dependent branching — which opcode dispatches next, which arm of each
handler runs — which `-O2`'s static heuristics cannot see. A profile gives the
compiler real edge frequencies, so it orders blocks (hot blocks contiguous for
I-cache), lays out branches, and makes inlining decisions from measured
behavior rather than guesses. CPython ships PGO for the same reason.

This document specifies the correct procedure for producing PGO builds and
integrating them into the wheel build matrix.

**See also:** [../archive/attr-lookup-cache.md](../archive/attr-lookup-cache.md) (the CALL_*_ATTR
dispatch-layout tax PGO is expected to dissolve),
[kio-setuptools-build.md](kio-setuptools-build.md) (extension build plumbing).


## Build modes

- **Dev / default — `-O2`.** The baseline: `-O2 -flto=auto` as today, with
  opcode handlers forced `always_inline`. Fast to build, and the correct
  default when no profile is available.
- **Release — PGO.** A three-pass PGO build (below) plus LTO, used for
  published wheels. Handlers become inline *hints* so the profile drives
  inlining.

PGO is opt-in and reserved for release artifacts. Dev builds and CI test runs
stay on `-O2` for build speed.


## The `always_inline` gate

Opcode handlers are declared `KOS_OP_INLINE`. Under PGO the profile knows which
handlers are hot, so forcing every one inline is counterproductive — it bloats
the dispatch function and denies the compiler the ability to outline cold
opcodes for I-cache relief. Gate the attribute on a `KOS_PGO` define that both
PGO passes set:

```c
#if defined(KOS_PGO)
# define KOS_OP_INLINE static inline            /* let the profile decide */
#else
# define KOS_OP_INLINE static inline __attribute__((always_inline))
#endif
```

Set `KOS_PGO` in **both** the instrument and use passes: the instrument build
needs distinct handler functions for the profile to count, and the use build
needs the freedom to inline per that profile. The non-PGO branch keeps the
forced inline, which is the correct default without a profile.


## Three-pass procedure (per target)

Each build target runs three passes:

1. **Instrument.** Build the extension with the toolchain's profile-generate
   flag and `-DKOS_PGO`.
2. **Profile.** Run the profiling harness against the instrumented build. It
   emits raw profile data matched to this exact toolchain, architecture, and
   Python version.
3. **Optimize.** Rebuild with the toolchain's profile-use flag, `-DKOS_PGO`,
   and LTO. This build produces the wheel.


## Toolchain branches

The harness is identical on every platform; only the flags and the profile
merge step differ.

- **Linux (GCC / gcov).** Instrument with `-fprofile-generate`; the run emits
  `.gcda`; optimize with `-fprofile-use -fprofile-correction`.
- **macOS (Clang / LLVM).** Instrument with `-fprofile-instr-generate`; the run
  emits `.profraw`; merge with `llvm-profdata merge` into `.profdata`; optimize
  with `-fprofile-instr-use=<merged>.profdata`.


## The profile is a per-target build intermediate

Profile data is matched to the exact compilation — per-function CFG checksums,
the compiler version, the target architecture, and the source variants the
Python-version macros select. It is therefore **regenerated inside each build**
and consumed immediately; it is a transient intermediate, not a versioned or
shipped file.

Every `(os × Python-version)` cell of the wheel matrix produces its own profile
from its own instrument-and-run pass, and thus its own optimally-profiled
wheel. The reusable, version-controlled asset is the harness that generates the
data, not the data.


## Profiling workload (the harness)

The harness is the one durable artifact here, and its composition determines
the quality of every PGO wheel. Compose it as a representative mix that weights
the universal hot machinery:

- **Broad coverage** — a subset of the phased test suite, so every opcode and
  path is exercised and the profile keeps the tail well-optimized.
- **Hot-path weight** — `churn --all`, repeated, to bias the profile toward
  dispatch, the `CALL_*` family, loop, and branch opcodes.
- **Realistic distributions** — a set of example Koskari programs, so real
  call / loop / namespace / exception mixes are represented.

Weight the harness toward what *every* program pays for — opcode dispatch,
namespace access, method calls — so PGO optimizes the common core that all user
code runs through.


## CI/CD integration

- **cibuildwheel.** Run the harness in a `CIBW_BEFORE_BUILD` step (or a custom
  build-backend hook) so each matrix cell performs the three-pass build in
  situ. Budget ~3× build time per cell, the accepted cost for PGO wheels.
- **Local.** A `make build-pgo` target runs the three passes for the current
  toolchain, for measurement and release rehearsal.
- **Scope.** PGO applies to published wheels only; dev and CI test builds stay
  on `-O2`.


## Integration points

- **`koskari/lib/hints.h`** — the `KOS_PGO` gate on `KOS_OP_INLINE`.
- **`setup.py`** — inject the profile-generate / profile-use compile and link
  flags, selected by a build-phase environment variable (e.g.
  `KOS_PGO_PHASE=generate|use`).
- **`make` / `tox`** — a `build-pgo` target orchestrating instrument → harness
  → optimize, with the GCC and Clang branches.
- **Harness entry point** — a runnable module/script that executes the mix
  above against the installed (instrumented) extension.


## The end.
