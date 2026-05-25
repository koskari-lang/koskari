
import asyncio
import json
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
    tp1 = bench_sync(loop_runs, churn_loop_p, 1_000_000, 1)
    announce("python churn_loop", tp1, tp1 / loop_runs)

    tp2 = bench_sync(tree_runs, churn_tree_p, 18, 50)
    announce("python churn_tree", tp2, tp2 / tree_runs)

    # Koskari versions
    tk1 = await bench_async(loop_runs, churn_loop_k, 1_000_000, 1)
    announce("koskari churn_loop", tk1, tk1 / loop_runs)

    tk2 = await bench_async(tree_runs, churn_tree_k, 18, 50)
    announce("koskari churn_tree", tk2, tk2 / tree_runs)

    ratio1 = tp1 / tk1
    ratio2 = tp2 / tk2
    print(f"churn_loop = {ratio1} ({(100 / ratio1):.2f}%)")
    print(f"churn_tree = {ratio2} ({(100 / ratio2):.2f}%)")


def print_trace_report_if_enabled():
    """
    If the extension was built with KOS_TRACE_EVENTS, print get_trace_report()
    JSON. Event values are cumulative counts, not wall time
    """

    report = koskari_lib.get_trace_report()
    if not report.get("flags", {}).get("events"):
        return

    with open("trace.json", "w") as f:
        json.dump(report, f, indent=2, default=str)
    print("get_trace_report() written to trace.json")


if __name__ == "__main__":
    # Trace accumulators include import/setup; clear so the report matches the
    # benchmarks below, not earlier koskari work
    koskari_lib.clear_trace_counters()
    asyncio.run(main())
    print_trace_report_if_enabled()


# The end.
