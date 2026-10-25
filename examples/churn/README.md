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

A **hosted** benchmark (`hosted=True` in its descriptor) is an async case:
each call is driven to completion on one asyncio event loop instead of the
`demand` path. The Python variant is an `async def`; the Koskari kernel is
called bare and its coroutine handed to the loop. The per-call
`run_until_complete` cost is paid on both sides.

## Selecting benchmarks

- **By name:** positional arguments — `tox -qe churn -- loop pidigits`.
- **By tag:** `--tag NAME` (repeatable) — `tox -qe churn -- --tag game --tag attr`.
- **All:** pass nothing.

Tags in use: `dispatch`, `call`, `keyword`, `method`, `recursion`,
`iteration`, `alloc`, `attr`, `slots`, `branch`, `match`, `loop`, `str`,
`demand`, `parameterize`, `coroutine`, `await`, `send`, `generator`, `float`,
`bignum`, `array`, `game`, `async`.

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
benchmark     py-best     py-med    ki-best     ki-med   ki/py
--------------------------------------------------------------
loop         11.1919s   11.1919s   17.3352s   17.3352s   1.57x
method        3.5169s    3.5169s   10.7684s   10.7684s   3.06x
```

The name column is as wide as the longest selected name. `ki/py` is
Koskari-best over Python-best (above 1.0 means Koskari is that many times
slower). Blank cells mean that variant did not run (a koskari-only benchmark,
or a `--mode` that runs one side). A `!! MISMATCH` suffix marks a failed
correctness check, and the run exits non-zero listing the offenders.

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
| `complex_branch` | `and`/`or`/`not` in loop, if/elif and value positions over compares and bare variables | — |
| `match_case` | eight-arm structural `match` over a rotating mix of subjects: class, sequence with rest, mapping, guarded and typed captures, strings, else | — |
| `strcat` | string built by `+` over three pieces and `+=` on one accumulator | — |
| `nbody` | float arithmetic (benchmarks-game n-body) | energy to 9 places |
| `fannkuch` | integer array indexing + mutation | `(16, 228)` at n=7 |
| `pidigits` | arbitrary-precision (bignum) arithmetic | first 50 digits of π |
| `binary_trees` | allocation churn + recursion | check-sum 129712 |
| `call_kw` | one call per element carrying a keyword argument | deterministic baseline |
| `starmap` | lazy `starmap` over bound methods nested two deep; Koskari's `starmap` on the Koskari side | deterministic baseline |
| `demand_call` | `map` over a two-argument function; the Koskari side goes through `demand`, one boundary call per element | deterministic baseline |
| `demand_call_kw` | `map` over `functools.partial` binding a keyword; the Koskari side goes through `demand` | deterministic baseline |
| `demand_call_method` | `map` over a bound method; the Koskari side goes through `demand` | deterministic baseline |
| `demand_call_param` | `demand_call` with one dynamic binding read per call: `parameterize` on the Koskari side, a `contextvars` binding on the Python side | deterministic baseline |
| `yieldfrom` | `yield-from` delegation, consumed fully and abandoned after one value | deterministic baseline |
| `coro_call` | hosted: `asyncio.gather` over one coroutine per element; the Koskari side gathers Koskari Coroutines | deterministic baseline |
| `coro_await` | hosted: serial `await` of one coroutine per element; the Koskari side awaits Koskari Coroutines | deterministic baseline |
| `coro_send` | `send(None)` into one coroutine per element, result taken from its StopIteration; the Koskari side sends into Koskari Coroutines | deterministic baseline |
| `async_tree` | hosted: fan-out task tree via `gather` (pyperformance async_tree, no I/O) | leaf count 4096 |
| `coroutines` | hosted: await-chain recursion (pyperformance coroutines) | fib(25) = 75025 |
| `async_generators` | hosted: async generator consumed by `async for` (pyperformance async_generators) | sum of range(20000) |

The first ten carry no external oracle — correctness rests
on the Python and Koskari answers agreeing. The four *game* benchmarks and three
of the *hosted* async benchmarks carry a known result, so they stay
correctness-checked even in `--mode koskari` with no Python side present.

The hosted kernels are written as Koskari, not as transliterated Python. Every
Koskari call already runs on the async-native evaluator, so `coroutines` is
the plain recursion and `async_generators` is a plain generator under
`for-each`; only `async_tree` carries an explicit `await`, on the
`asyncio.gather` future, which is a handle the evaluator never resolves on its
own.

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
       hosted=False,           # True => drive each call on an event loop
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
