
import koskari

from churn_p import churn_loop as churn_loop_p, churn_tree as churn_tree_p
from churn_k import churn_loop as churn_loop_k, churn_tree as churn_tree_k

import asyncio
import time
import timeit

RUNS = 10


async def bench_async(fn, *args):
    t0 = time.perf_counter()
    for _ in range(RUNS):
        answer = await fn(*args)
    t1 = time.perf_counter()

    print(answer)
    return t1 - t0


def bench_sync(fn, *args):
    t0 = time.perf_counter()
    for _ in range(RUNS):
        answer = fn(*args)
    t1 = time.perf_counter()

    print(answer)
    return t1 - t0


def announce(name, total, avg):
    print(f"{name:20s} total={total:.6f}s  avg={avg:.6f}s")


async def main():
    print("Running benchmarks...")

    # Python versions
    t = bench_sync(churn_loop_p, 5_000_000, 1)
    announce("python churn_loop", t, t / RUNS)

    t = bench_sync(churn_tree_p, 18, 50)
    announce("python churn_tree", t, t / RUNS)

    # Koskari versions
    t = await bench_async(churn_loop_k, 5_000_000, 1)
    announce("koskari churn_loop", t, t / RUNS)

    t = await bench_async(churn_tree_k, 18, 50)
    announce("koskari churn_tree", t, t / RUNS)


asyncio.run(main())