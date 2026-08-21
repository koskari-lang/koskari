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
tools.perfstat

Hardware-counter collection over the churn kernels, following the
methodology in design/dispatch-sizing.md. The stat mode runs perf stat
per kernel with the bootstrap floor subtracted from the deterministic
instruction count and appends one row per kernel to a TSV log. The
opcodes mode runs perf record over all kernels and reports per-opcode
cycle shares; it expects the current build to be a KOS_ALL_COLD build,
where every handler is its own symbol

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import argparse
import os
import subprocess
import sys
from datetime import date


EVENTS = "instructions:u,cycles:u,branch-misses:u,L1-icache-load-misses:u"
EVENT_KEYS = ("instructions:u", "cycles:u", "branch-misses:u",
              "L1-icache-load-misses:u")

CHURN = os.path.join("examples", "churn")


def kernel_names():
    """
    The churn benchmark names, from the harness's own registry
    """

    sys.path.insert(0, CHURN)
    import registry
    return list(registry.discover())


def churn_command(python, name, samples, warmup):
    return [
        python, "-B", os.path.join(CHURN, "churn.py"),
        "--mode", "koskari",
        "--samples", str(samples), "--warmup", str(warmup),
        name,
    ]


def perf_stat(cmd):
    """
    Run cmd under perf stat and return {event: count}. Unsupported or
    uncounted events come back as None. The command's own exit status
    is ignored; the bootstrap-floor run fails by design

    :param cmd: argv to run under perf stat
    :return: mapping of event name to count or None
    """

    proc = subprocess.run(
        ["perf", "stat", "-x", ",", "-e", EVENTS, "--"] + cmd,
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)

    counts = {}
    for line in proc.stderr.splitlines():
        parts = line.split(",")
        if len(parts) < 3:
            continue
        value, _unit, event = parts[0], parts[1], parts[2]
        if event in EVENT_KEYS:
            try:
                counts[event] = int(value)
            except ValueError:
                counts[event] = None
    return counts


def git_sha():
    proc = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"],
        capture_output=True, text=True)
    return proc.stdout.strip() or "unknown"


def run_stat(opts):
    """
    Per-kernel perf stat with the bootstrap floor subtracted from the
    instruction count, appended to the TSV log
    """

    names = opts.names or kernel_names()
    sha = git_sha()
    today = date.today().isoformat()

    print("measuring bootstrap floor...")
    floor = perf_stat(churn_command(opts.python, names[0], 0, 0))
    floor_instr = floor.get("instructions:u") or 0
    print(f"floor: {floor_instr:,} instructions")

    header = ("date\tsha\tkernel\tinstructions\tinstr_minus_floor"
              "\tcycles\tbranch_misses\tl1i_misses\n")
    rows = []
    for name in names:
        counts = perf_stat(
            churn_command(opts.python, name, opts.samples, opts.warmup))
        instr = counts.get("instructions:u")
        delta = instr - floor_instr if instr is not None else None
        row = (today, sha, name, instr, delta,
               counts.get("cycles:u"),
               counts.get("branch-misses:u"),
               counts.get("L1-icache-load-misses:u"))
        rows.append(row)
        print(f"{name:16s} instr {instr or 0:>16,}  "
              f"minus floor {delta or 0:>16,}")

    os.makedirs(os.path.dirname(opts.log), exist_ok=True)
    fresh = not os.path.exists(opts.log)
    with open(opts.log, "a") as fh:
        if fresh:
            fh.write(header)
        for row in rows:
            fh.write("\t".join("" if v is None else str(v)
                               for v in row) + "\n")
    print(f"appended {len(rows)} rows to {opts.log}")


def run_opcodes(opts):
    """
    Per-opcode cycle shares by sampling: perf record over every kernel
    in one process, reported by symbol and filtered to the op_ handler
    symbols. Meaningful only on a KOS_ALL_COLD build
    """

    names = opts.names or kernel_names()
    data = os.path.join("build", "perf-opcodes.data")
    os.makedirs("build", exist_ok=True)

    cmd = [
        opts.python, "-B", os.path.join(CHURN, "churn.py"),
        "--mode", "koskari",
        "--samples", str(opts.samples), "--warmup", str(opts.warmup),
    ] + names
    print("recording:", " ".join(names))
    subprocess.run(
        ["perf", "record", "-e", "cycles:u", "-F", "999",
         "-o", data, "--"] + cmd,
        stdout=subprocess.DEVNULL, check=True)

    proc = subprocess.run(
        ["perf", "report", "--stdio", "--no-children",
         "--percent-limit", "0.01", "-i", data],
        capture_output=True, text=True, check=True)

    lines = [line for line in proc.stdout.splitlines()
             if " op_" in line]
    with open(opts.out, "w") as fh:
        fh.write(f"# per-opcode cycle shares, {date.today().isoformat()}"
                 f" at {git_sha()} (KOS_ALL_COLD sampling build)\n")
        fh.write("\n".join(lines) + "\n")

    print(f"\ntop opcode handlers by cycles ({opts.out}):")
    for line in lines[:30]:
        print(line)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="perf collection over the churn kernels")
    parser.add_argument(
        "mode", choices=("stat", "opcodes"),
        help="stat: per-kernel counters; opcodes: per-op cycle shares")
    parser.add_argument(
        "names", nargs="*", metavar="NAME",
        help="kernels to run (default: all)")
    parser.add_argument(
        "--python", default=os.path.join(".venv", "bin", "python"),
        help="interpreter to run the harness with")
    parser.add_argument(
        "--samples", type=int, default=1,
        help="measured batches per kernel (default: 1)")
    parser.add_argument(
        "--warmup", type=int, default=1,
        help="warmup batches per kernel (default: 1)")
    parser.add_argument(
        "--log", default=os.path.join("design", "perf", "perf.tsv"),
        help="TSV log for stat mode")
    parser.add_argument(
        "--out", default=os.path.join("build", "perf-opcodes.txt"),
        help="report path for opcodes mode")
    opts = parser.parse_args(argv)

    if opts.mode == "stat":
        run_stat(opts)
    else:
        run_opcodes(opts)
    return 0


if __name__ == "__main__":
    sys.exit(main())


# The end.
