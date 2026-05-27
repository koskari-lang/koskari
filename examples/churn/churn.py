
import argparse
import asyncio
import json
import time

import koskari

from churn_k import churn_list_each as churn_list_each_k
from churn_k import churn_loop as churn_loop_k
from churn_k import churn_tree as churn_tree_k
from churn_p import churn_list_each as churn_list_each_p
from churn_p import churn_loop as churn_loop_p
from churn_p import churn_tree as churn_tree_p

from koskari.lib import clear_trace_counters, demand, get_trace_report


LABELS = {
    'loop': 'churn_loop',
    'tree': 'churn_tree',
    'list_each': 'churn_list_each',
}


BENCHMARKS = {
    'loop': {
        'runs': 50,
        'args': (1_000_000, 1),
        'py': churn_loop_p,
        'ki': churn_loop_k,
    },
    'tree': {
        'runs': 50,
        'args': (18, 50),
        'py': churn_tree_p,
        'ki': churn_tree_k,
    },
    'list_each': {
        'runs': 50,
        'args': (1_000_000, 1),
        'py': churn_list_each_p,
        'ki': churn_list_each_k,
    },
}


def bench_async(runs, fn, *args):
    t0 = time.perf_counter()
    fn = demand(fn)

    for _ in range(runs):
        answer = fn(*args)
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


def parse_args_usage():
    return (
        'examples:\n'
        '  churn.py --all\n'
        '  churn.py --loop\n'
        '  churn.py --tree\n'
        '  churn.py --list-each\n'
        '  churn.py --loop --list-each')


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description='Compare Python and Koskari churn benchmarks',
        epilog=parse_args_usage(),
        formatter_class=argparse.RawDescriptionHelpFormatter)

    parser.add_argument(
        '--all',
        action='store_true',
        help='Run all benchmark pairs (default when no pair flag is given)')
    parser.add_argument(
        '--loop',
        action='store_true',
        help='Run churn_loop (Python while vs Koskari while)')
    parser.add_argument(
        '--tree',
        action='store_true',
        help='Run churn_tree (recursive call depth)')
    parser.add_argument(
        '--list-each',
        action='store_true',
        dest='list_each',
        help='Run churn_list_each (list comp vs list-each)')

    args = parser.parse_args(argv)

    selected = []
    if args.loop:
        selected.append('loop')
    if args.tree:
        selected.append('tree')
    if args.list_each:
        selected.append('list_each')
    if not selected:
        selected = list(BENCHMARKS.keys())

    return selected


def run_pair(name):
    bench = BENCHMARKS[name]
    runs = bench['runs']
    args = bench['args']
    label = LABELS[name]

    tp = bench_sync(runs, bench['py'], *args)
    announce('python %s' % label, tp, tp / runs)

    tk = bench_async(runs, bench['ki'], *args)
    announce('koskari %s' % label, tk, tk / runs)

    ratio = tp / tk
    print('%s = %s (%.2f%%)' % (label, ratio, 100 / ratio))
    return tp, tk


async def main(selected):
    print('Running benchmarks: %s' % ', '.join(selected))

    for name in selected:
        run_pair(name)


def print_trace_report_if_enabled():
    """
    If the extension was built with KOS_TRACE_EVENTS, print get_trace_report()
    JSON. Event values are cumulative counts, not wall time
    """

    report = get_trace_report()
    if not report.get('flags', {}).get('events'):
        return

    with open('trace.json', 'w') as f:
        json.dump(report, f, indent=2, default=str)
    print('get_trace_report() written to trace.json')


if __name__ == '__main__':
    selected = parse_args()
    clear_trace_counters()
    asyncio.run(main(selected))
    print_trace_report_if_enabled()


# The end.
