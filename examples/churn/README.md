# Churn — Koskari microbenchmark harness

Churn measures the Koskari evaluator against equivalent Python, one focused
kernel at a time. Each benchmark exists in two languages — a Python variant and
a self-contained Koskari kernel — so a run both **times** the two and
**cross-checks** that they compute the same answer. Several benchmarks also
carry a known result (an *oracle*), so they double as correctness proofs.

It is built for two jobs:

- **Comparison** -- the `ki/py` ratio on a given path, and whether that
  ratio moves across a change. Below 1.0 Koskari is faster; above 1.0
  it is slower.
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
`bignum`, `array`, `game`, `async`, `continuation`.

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

A headed table, one row per benchmark. The block below is a full
`tox -qe churn` from 2026-10-27 (`samples=5`, `warmup=1`). Each row is a
batch of many kernel calls, not a single iteration:

```
mode=compare  samples=5  warmup=1
benchmarks: async_generators, async_tree, binary_trees, branch, call_kw, complex_branch, coro_await, coro_call, coro_send, coroutines, demand_call, demand_call_kw, demand_call_method, demand_call_param, fannkuch, field, field_slots, list_each, loop, match_case, method, nbody, pidigits, starmap, strcat, tree, yieldfrom
benchmark             py-best     py-med    ki-best     ki-med   ki/py
----------------------------------------------------------------------
async_generators      0.0796s    0.0801s    0.0378s    0.0381s   0.47x
async_tree            0.1733s    0.1756s    0.2071s    0.2127s   1.19x
binary_trees          0.0513s    0.0515s    0.0737s    0.0743s   1.44x
branch                5.5097s    5.6028s    4.4946s    4.5103s   0.82x
call_kw               0.0259s    0.0260s    0.0218s    0.0219s   0.84x
complex_branch        8.6746s    8.6892s    5.5607s    5.7070s   0.64x
coro_await            0.0030s    0.0030s    0.0058s    0.0058s   1.91x
coro_call             0.0449s    0.0451s    0.0466s    0.0490s   1.04x
coro_send             0.0047s    0.0048s    0.0070s    0.0071s   1.49x
coroutines            0.1188s    0.1192s    0.0576s    0.0577s   0.48x
demand_call           0.0239s    0.0242s    0.0231s    0.0231s   0.96x
demand_call_kw        0.0335s    0.0338s    0.0397s    0.0400s   1.19x
demand_call_method    0.0249s    0.0252s    0.0249s    0.0251s   1.00x
demand_call_param     0.0254s    0.0255s    0.0266s    0.0269s   1.05x
fannkuch              0.1270s    0.1277s    0.1573s    0.1573s   1.24x
field                 4.2573s    4.2753s    2.3418s    2.3478s   0.55x
field_slots           4.2751s    4.2838s    2.1867s    2.1929s   0.51x
list_each            11.5489s   11.5887s    7.7081s    7.7156s   0.67x
loop                 10.9879s   11.0237s    6.2596s    6.2768s   0.57x
match_case            0.3718s    0.3731s    0.1730s    0.1735s   0.47x
method                3.4788s    3.4939s    3.0461s    3.0523s   0.88x
nbody                 1.3306s    1.3347s    1.2723s    1.2738s   0.96x
pidigits              0.0044s    0.0044s    0.0061s    0.0062s   1.41x
starmap               0.0048s    0.0048s    0.0085s    0.0085s   1.78x
strcat                5.0929s    5.1012s    4.8615s    4.8653s   0.95x
tree                  5.7583s    5.7742s    3.7183s    3.7344s   0.65x
yieldfrom             0.0069s    0.0070s    0.0109s    0.0109s   1.56x
```

The name column is as wide as the longest selected name. `ki/py` is
Koskari-best over Python-best: below 1.0 Koskari is faster by that
factor, above 1.0 slower. On this run Koskari was faster on 15 kernels,
at 1.00x on `demand_call_method`, and slower on 11. Every kernel whose
Python side took more than a second was faster in Koskari. The low
ratios are `async_generators`, `coroutines`, and `match_case`
(0.47–0.48x); the high ones are `coro_await` (1.91x), `starmap`
(1.78x), and `yieldfrom` (1.56x). Blank cells mean that variant did not
run (a koskari-only benchmark, or a `--mode` that runs one side). A
`!! MISMATCH` suffix marks a failed correctness check, and the run
exits non-zero listing the offenders.

`--json` writes the same data machine-readably: `mode`, `samples`, `warmup`,
and a `results` list, each entry carrying `tags`, `ok`, `expected`, the
`ratio_koskari_over_python`, and per-variant `best`/`median`/`total`/`per_call`/
`answer`.

## The benchmarks

| Benchmark | Exercises | Oracle |
|-----------|-----------|--------|
| `loop` | tight while loop, arithmetic dispatch | — |
| `tree` | recursive call depth, frame churn | — |
| `tail` | self tail call per step, frame reuse | — |
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
| `reset_plain` | one `reset` per step whose body never shifts; a `try` block that never raises on the Python side | — |
| `shift_abort` | one `abort` per step, through one call; a raise caught one frame up on the Python side | — |
| `shift_generator` | a producer that shifts each value out with its continuation, resumed once per value; a generator stepped with `next` on the Python side | — |
| `shift_amb` | a three-level search where each choice is a `shift` whose handler calls the continuation once per choice; three nested loops on the Python side | the six Pythagorean triples under 21 |
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
   def square_sum [n]:
       var acc 0
       var i 0
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
