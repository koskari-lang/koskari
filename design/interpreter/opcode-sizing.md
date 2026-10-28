# Opcode sizing and timing

Per-handler code size joined with per-opcode time, kept in
[`design/perf/opsizing.tsv`](../perf/opsizing.tsv) beside the perf
counter log. The tool is `tools/opsizing.ki`; the table is regenerated
by `make opsizing` and committed with the trace that fed it. The
inlined-dispatch era's analysis, with its hot/cold split and inline
budget, is archived in
[archive/dispatch-sizing.md](../archive/dispatch-sizing.md).

## Regeneration

`make opsizing` builds the `perf-opcodes` tox env (an ordinary build;
every handler is its own symbol) and the `churn-timing` tox env
(`KOS_TRACE_TIMING=1`, nothing else traced), runs the churn kernels
under the timing build, files the trace as
`design/trace/trace-YYYYMMDD-HHMM.json`, and writes the table.
`PERF_ARGS="loop nbody"` limits the sweep to named kernels. Each tox
env owns its build, so no flag toggling touches the dev venv.

`tox -qe opsizing -- --trace PATH` re-joins an existing trace against
the current cold build without a sweep. `--top N --sort bytes` changes
the console view only; the file always holds every opcode.

## Columns

| column | source | meaning |
|---|---|---|
| `bytes` | `nm --print-size` on the cold build | standalone size of `op_NAME`, compiler suffixes (`.lto_priv`, `.cold`) folded in; empty when the build has no such symbol |
| `count` | trace | executions over the sweep |
| `avg_ns` | trace | mean time from entering the handler to entering the next |
| `net_ns` | derived | `avg_ns` minus the trace's `step_overhead_ns` |
| `total_ns` | trace | `count` times `avg_ns` |
| `time_pct`, `exec_pct` | trace | share of the sweep's time and executions |

Rows are sorted by name, so a regeneration diffs by row. Two pseudo
rows have no size: `resume` is the time from a resume hop to the next
opcode, `trampoline` the time spent in signal returns and re-entry.

## Reading it

- `net_ns` at or near zero means the handler body costs nothing
  beyond the transfer itself; the loads (`FAST`, `CONST` and their
  fused forms) sit there. Fusing such opcodes saves a transfer each,
  not body time.
- `bytes` is the standalone size, a proxy: LTO still inlines callees
  into the handler and specialises away code. Shared static cores
  (`object_next_step` at the time of measurement, since replaced by
  `KosContext_NextResume`; the operator cores `op_add` and kin) are
  outside the table.
- Shares are of the churn workload only. The library-shaped workloads
  (scheduler, mcp, repl, dis) have a different opcode mix; `tox -qe
  opgrams` shows their emission counts.
- Absolute nanoseconds are for this machine and build; compare rows
  within one table, or the same row across tables from the same
  machine.

## Measuring a change

Use `perf stat -e instructions:u` over the churn kernels (`make
perf`), not wall clock. Wall clock on this workload cannot separate a
change from code layout: repeat runs of an identical binary varied by
up to 2.5%, and a change with fewer instructions has measured slower.
Instruction counts are deterministic, so one run per build suffices.
`make perf` subtracts the bootstrap floor (a `--samples 0 --warmup 0`
run) and appends to `design/perf/perf.tsv`.

The timing build is for attributing time, not for measuring a change:
its stamps add a fixed cost per transfer and inflate the sweep.

Forcing `eval_plain_return` back inline was measured and rejected
(8,886 B; the body owns frame teardown and drags `kos_dynframe_free`,
`KosStack_Release` and `ns_dealloc` in with it). Splitting it into an
always-inlined core plus an out-of-line wrapper measured byte for byte
identical. Do not re-try this.
