
import asyncio
import json
import koskari
import koskari.lib as koskari_lib
import sys
import time

from churn_p import churn_loop as churn_loop_p, churn_tree as churn_tree_p
from churn_k import churn_loop as churn_loop_k, churn_tree as churn_tree_k


async def bench_async(runs, fn, *args):
    t0 = time.perf_counter()
    for _ in range(runs):
        answer = await fn(*args)
    t1 = time.perf_counter()

    print(answer)
    return t1 - t0


def bench_sync(runs, fn, *args):
    t0 = time.perf_counter()
    for _ in range(runs):
        answer = fn(*args)
    t1 = time.perf_counter()

    print(answer)
    return t1 - t0


def announce(name, total, avg):
    print(f"{name:20s} total={total:.6f}s  avg={avg:.6f}s")


async def main():
    print("Running benchmarks...")

    loop_runs = 50
    tree_runs = 50

    # Python versions
    t = bench_sync(loop_runs, churn_loop_p, 1_000_000, 1)
    announce("python churn_loop", t, t / loop_runs)

    t = bench_sync(tree_runs, churn_tree_p, 18, 50)
    announce("python churn_tree", t, t / tree_runs)

    # Koskari versions
    t = await bench_async(loop_runs, churn_loop_k, 1_000_000, 1)
    announce("koskari churn_loop", t, t / loop_runs)

    t = await bench_async(tree_runs, churn_tree_k, 18, 50)
    announce("koskari churn_tree", t, t / tree_runs)


def print_trace_report_if_enabled():
    """
    If the extension was built with KOS_TRACE_EVENTS, print get_trace_report()
    JSON. Event values are cumulative counts, not wall time
    """

    report = koskari_lib.get_trace_report()
    if not report.get("flags", {}).get("events"):
        return

    sys.stdout.flush()
    print(file=sys.stderr)
    print(
        "--- get_trace_report() (events are cumulative counts, not wall time) ---",
        file=sys.stderr,
    )
    print(json.dumps(report, indent=2, default=str))


if __name__ == "__main__":
    # Trace accumulators include import/setup; clear so the report matches the
    # benchmarks below, not earlier koskari work
    koskari_lib.clear_trace_counters()
    asyncio.run(main())
    print_trace_report_if_enabled()


# The end.
