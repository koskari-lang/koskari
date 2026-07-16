# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
churn -- Koskari microbenchmark harness.

Runs a family of self-contained benchmarks in one of three modes:

  compare  Python and Koskari, answers cross-checked (the default).
  koskari  Koskari only -- no Python cost; the tracing path.
  python   the reference variant alone.

Benchmarks are discovered from the sibling ``py`` package; each is paired
with a Koskari kernel of the same base name in the ``ki`` package.  Select
by name (positional), by ``--tag``, or run all.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import argparse
import json
from time import perf_counter

import koskari  # noqa: F401  (activates the .ki import hook)

from koskari.lib import clear_trace_counters, demand, get_trace_report

import registry
import reporter


def measure(fn, args, runs, samples, warmup):
    """
    Time ``fn(*args)`` in batches of ``runs`` calls.  Discard ``warmup``
    batches, then record ``samples`` batches.  Return the per-batch times and
    the last answer produced.
    """

    times = []
    answer = None
    for index in range(warmup + samples):
        start = perf_counter()
        for _ in range(runs):
            answer = fn(*args)
        stop = perf_counter()
        if index >= warmup:
            times.append(stop - start)
    return times, answer


def run_benchmark(bench, mode, samples, warmup):
    """
    Run one benchmark under ``mode`` and return a :class:`reporter.Result`.
    """

    want_py = mode in ("compare", "python") and bench.py is not None
    want_ki = mode in ("compare", "koskari")

    if not want_py and not want_ki:
        return reporter.Result(
            name=bench.name, mode=mode, tags=bench.tags,
            expected=bench.expected, timings=[], ok=True,
            note="skipped (koskari-only case, no python variant)")

    timings = []
    answers = []

    if want_py:
        times, answer = measure(
            bench.py, bench.args, bench.runs, samples, warmup)
        timings.append(reporter.Timing("python", bench.runs, times, answer))
        answers.append(answer)

    if want_ki:
        fn = demand(bench.ki)
        times, answer = measure(fn, bench.args, bench.runs, samples, warmup)
        timings.append(reporter.Timing("koskari", bench.runs, times, answer))
        answers.append(answer)

    ok = True
    for answer in answers:
        if answer != answers[0]:
            ok = False
        if bench.expected is not None and answer != bench.expected:
            ok = False

    return reporter.Result(
        name=bench.name, mode=mode, tags=bench.tags,
        expected=bench.expected, timings=timings, ok=ok)


def select(registry_map, names, tags):
    """
    Resolve the requested benchmark names.  Explicit names win; else any
    benchmark carrying one of ``tags``; else every benchmark.
    """

    if names:
        for name in names:
            if name not in registry_map:
                raise SystemExit("unknown benchmark: %s" % name)
        return list(names)

    if tags:
        wanted = set(tags)
        return [name for name, bench in registry_map.items()
                if wanted.intersection(bench.tags)]

    return list(registry_map.keys())


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="Run Koskari churn microbenchmarks")
    parser.add_argument(
        "names", nargs="*", metavar="NAME",
        help="benchmarks to run (default: all)")
    parser.add_argument(
        "--tag", action="append", default=[], metavar="TAG",
        help="select benchmarks carrying this tag (repeatable)")
    parser.add_argument(
        "--mode", choices=("compare", "koskari", "python"), default="compare",
        help="compare both, koskari only (tracing), or python only")
    parser.add_argument(
        "--samples", type=int, default=5,
        help="measured batches per variant (default: 5)")
    parser.add_argument(
        "--warmup", type=int, default=1,
        help="warmup batches discarded before measuring (default: 1)")
    parser.add_argument(
        "--json", dest="json_path", default=None, metavar="PATH",
        help="write the metrics payload to PATH")
    return parser.parse_args(argv)


def run(opts):
    """
    Discover, select, and run the benchmarks.  Return the list of names that
    failed their correctness check.
    """

    registry_map = registry.discover()
    selected = select(registry_map, opts.names, opts.tag)

    print("mode=%s  samples=%d  warmup=%d"
          % (opts.mode, opts.samples, opts.warmup))
    print("benchmarks: %s" % ", ".join(selected))

    results = []
    failures = []
    print(reporter.format_header())
    for name in selected:
        result = run_benchmark(
            registry_map[name], opts.mode, opts.samples, opts.warmup)
        results.append(result)
        print(reporter.format_result(result))
        if not result.ok:
            failures.append(name)

    if opts.json_path:
        payload = reporter.to_json(
            results, opts.mode, opts.samples, opts.warmup)
        with open(opts.json_path, "w") as out:
            json.dump(payload, out, indent=2, default=str)
        print("metrics written to %s" % opts.json_path)

    return failures


def print_trace_report_if_enabled():
    """
    If the extension was built with KOS_TRACE_EVENTS, write get_trace_report()
    JSON to trace.json.  Event values are cumulative counts, not wall time.
    """

    report = get_trace_report()
    if not report.get("flags", {}).get("events"):
        return
    with open("trace.json", "w") as out:
        json.dump(report, out, indent=2, default=str)
    print("get_trace_report() written to trace.json")


if __name__ == "__main__":
    options = parse_args()
    clear_trace_counters()
    failed = run(options)
    print_trace_report_if_enabled()
    if failed:
        raise SystemExit("correctness mismatch: %s" % ", ".join(failed))


# The end.
