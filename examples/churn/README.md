# Churn — Koskari microbenchmark harness

Churn measures the Koskari evaluator against equivalent Python, one focused
kernel at a time. Each benchmark exists in two languages — a Python variant and
a self-contained Koskari kernel — so a run both **times** the two and
**cross-checks** that they compute the same answer. Several benchmarks also
carry a known result (an *oracle*), so they double as correctness proofs.

It is built for two jobs:

- **Comparison** — how much slower is Koskari than Python on a given path, and
  does that ratio move across a change.
- **Tracing** — run Koskari alone (no Python cost) under a trace-instrumented
  build to see opcode-cache, timing, and eval-sync counters.

## Running

All Python here goes through **tox** — never invoke `python`/`pytest`
directly. Two environments drive the harness:

```bash
# Comparison: Python vs Koskari, answers cross-checked (default mode)
tox -qe churn                      # all benchmarks
tox -qe churn -- loop tree method  # a subset, by name
tox -qe churn -- --tag game        # a family, by tag
tox -qe churn -- --json out.json   # also write a metrics payload

# Tracing: Koskari only, with the KOS_TRACE_* counters
tox -qe churn-trace -- nbody
```

`tox -qe churn` prepends `--mode compare`; `tox -qe churn-trace` prepends
`--mode koskari`. Everything after `--` is passed straight to the driver, so
any flag below works, and a later `--mode ...` overrides the default (argparse
takes the last value).

> **Tracing needs a matching build.** The `KOS_TRACE_*` toggles are
> *compile-time* (`setup.py` turns them into `-D` flags), so `churn-trace` only
> produces counters after the extension is rebuilt with those flags set. When
> built with `KOS_TRACE_EVENTS`, the run writes `trace.json` next to the driver.

## Modes

| Mode | Runs | Checks | Use |
|------|------|--------|-----|
| `compare` | Python **and** Koskari | `py == ki`, and both `== expected` when set | default; regression + ratio |
| `koskari` | Koskari only | `== expected` when set | tracing; no Python cost |
| `python` | Python only | `== expected` when set | profile the reference alone |

Koskari-only benchmarks (no Python variant) run their Koskari side in `compare`
too, checking `expected`; they are skipped only under `--mode python`.

## Selecting benchmarks

- **By name:** positional arguments — `tox -qe churn -- loop pidigits`.
- **By tag:** `--tag NAME` (repeatable) — `tox -qe churn -- --tag game --tag attr`.
- **All:** pass nothing.

Tags in use: `dispatch`, `call`, `recursion`, `iteration`, `alloc`, `attr`,
`slots`, `branch`, `loop`, `demand`, `generator`, `koskari-only`, `float`,
`bignum`, `array`, `game`.

## Options

| Flag | Default | Meaning |
|------|---------|---------|
| `--mode {compare,koskari,python}` | `compare` | which variants to run |
| `--samples N` | 5 | measured batches per variant |
| `--warmup N` | 1 | warmup batches discarded before measuring |
| `--json PATH` | — | write the metrics payload to PATH |

Each *batch* times `runs` calls of the kernel (`runs` is fixed per benchmark).
The harness reports the **best** (minimum — the stable signal for a
compute-bound loop) and **median** batch time.

## Output

A headed table, one row per benchmark:

```
benchmark         py-best     py-med    ki-best     ki-med   ki/py
------------------------------------------------------------------
loop              11.1919s   11.1919s   17.3352s   17.3352s   1.57x
method             3.5169s    3.5169s   10.7684s   10.7684s   3.06x
starmap                                  0.0219s    0.0219s
```

`ki/py` is Koskari-best over Python-best (above 1.0 means Koskari is that many
times slower). Blank cells mean that variant did not run (here, a koskari-only
benchmark). A `!! MISMATCH` suffix marks a failed correctness check, and the
run exits non-zero listing the offenders.

`--json` writes the same data machine-readably: `mode`, `samples`, `warmup`,
and a `results` list, each entry carrying `tags`, `ok`, `expected`, the
`ratio_koskari_over_python`, and per-variant `best`/`median`/`total`/`per_call`/
`answer`.

## The benchmarks

| Benchmark | Exercises | Oracle |
|-----------|-----------|--------|
| `loop` | tight while loop, arithmetic dispatch | — |
| `tree` | recursive call depth, frame churn | — |
| `list_each` | comprehension build + fold | — |
| `method` | bound-method call + attr resolution | — |
| `field` | instance-attribute read/modify/write (dict path) | — |
| `field_slots` | same, on a `__slots__` class (SLOT path) | — |
| `branch` | four-arm if/elif/else density | — |
| `nbody` | float arithmetic (benchmarks-game n-body) | energy to 9 places |
| `fannkuch` | integer array indexing + mutation | `(16, 228)` at n=7 |
| `pidigits` | arbitrary-precision (bignum) arithmetic | first 50 digits of π |
| `binary_trees` | allocation churn + recursion | check-sum 129712 |
| `starmap` | Koskari-only: the `demand` sync path | deterministic baseline |
| `yieldfrom` | Koskari-only: `yield-from` delegation/teardown | deterministic baseline |

The first seven are RNG-style churn with no external oracle — correctness rests
on the Python and Koskari answers agreeing. The four *game* benchmarks carry a
known checksum, so they stay correctness-checked even in `--mode koskari` with
no Python side present.

## Adding a benchmark

A benchmark is two files sharing a base name — nothing else is edited, because
the driver discovers every non-underscore module in `py/`.

1. **Kernel** — `ki/<name>.ki`, a self-contained Koskari function:

   ```koskari
   ;; ki/square_sum.ki
   define square_sum [n]:
       define acc 0
       define i 0
       while (< i n):
           set! acc (+ acc (* i i))
           set! i (+ i 1)
       acc
   ```

2. **Variant + descriptor** — `py/<name>.py`, the Python variant (or `None`
   for a Koskari-only case) and the `BENCHMARK` the driver reads:

   ```python
   from ki import square_sum as _ki
   from registry import Benchmark


   def square_sum(n):
       acc = 0
       for i in range(n):
           acc += i * i
       return acc


   BENCHMARK = Benchmark(
       name="square_sum",
       runs=50,
       args=(1_000_000,),
       py=square_sum,          # None => koskari-only
       ki=_ki.square_sum,
       expected=None,          # a known value makes it self-validating
       tags=("loop",),
   )
   ```

That is it — `tox -qe churn -- square_sum` now runs it. Match the base names
(`ki/square_sum.ki` ↔ `py/square_sum.py`). **Name the entry function with an
underscore**, not a dash: Koskari does not fold `square-sum` to `square_sum` on
Python attribute access, so `_ki.square_sum` only resolves if the kernel defines
`square_sum`. Internal helpers the kernel calls itself may use dashes freely.

For a **float** kernel, write every constant with the *identical* decimal
string in both languages and round the result (e.g. `(round x 9)`) so the two
doubles compare equal. For a Koskari-only case, capture the deterministic
result once (via `--json`) and set it as `expected`.

## Layout

```
churn.py        driver: modes, discovery, measurement
registry.py     the Benchmark descriptor + discover()
reporter.py     Timing/Result records, the table, the JSON payload
py/             Python variants + descriptors (import koskari)
ki/             self-contained Koskari kernels
```

`py/` and `ki/` are separate top-level packages with distinct names on purpose:
a `foo.py` and a `foo.ki` in one directory would both claim the module name
`foo` and conflict, so same-named kernels live in sibling packages instead. Each
has an `__init__.py` that runs `import koskari`, which both wins over any
installed `py`/`ki` distribution and guarantees the `.ki` import hook is active.
