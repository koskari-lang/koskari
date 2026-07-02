# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
vgkos - filter a valgrind log down to records involving the koskari
native extension.

CPython's normal shutdown leaves an unbounded, version-dependent pile of
"definitely lost" records; chasing them with suppressions is a
treadmill. For launcher-driven runs (e.g. valgrind over
`koskari script.ki`) the actionable signal is just the records whose
backtrace crosses koskari's extension module. Usage:

    python tools/vgkos.py valgrind.log [--needle SUBSTR]

Prints matching records grouped by their first koskari frame, with a
summary count. Exit status 1 when any matching leak record is found.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-fable-5 via Claude Code
"""


import re
import sys

from argparse import ArgumentParser
from collections import Counter, defaultdict


_PID = re.compile(r"^==\d+==\s?")
_SYM = re.compile(r"(?:at|by) 0x[0-9A-Fa-f]+: (\S+)")


def read_records(path):
    """
    Yield valgrind records as lists of pid-stripped lines.
    """

    record = []
    with open(path, errors="replace") as fh:
        for line in fh:
            line = _PID.sub("", line.rstrip("\n"))
            if not line.strip():
                if record:
                    yield record
                    record = []
            else:
                record.append(line)
    if record:
        yield record


def first_needle_symbol(record, needle):
    """
    Symbol of the first frame whose line mentions needle, or None.
    """

    for line in record:
        if needle in line:
            found = _SYM.search(line)
            return found.group(1) if found else "?"
    return None


def main(argv=None):
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("logfile")
    parser.add_argument(
        "--needle", default="koskari/lib.cpython",
        help="substring identifying frames of interest")
    opts = parser.parse_args(argv)

    groups = defaultdict(list)
    counts = Counter()
    total = 0

    for record in read_records(opts.logfile):
        total += 1
        if "lost in loss record" not in record[0]:
            continue
        sym = first_needle_symbol(record, opts.needle)
        if sym is None:
            continue
        counts[sym] += 1
        groups[sym].append(record)

    for sym, records in sorted(groups.items()):
        for record in records:
            print(record[0])
            for line in record[1:]:
                print("   ", line)
            print()

    matched = sum(counts.values())
    print(f"{total} records scanned; {matched} leak record(s) matched"
          f" {opts.needle!r}")
    for sym, n in counts.most_common():
        print(f"  {n:4d}  {sym}")

    return 1 if matched else 0


if __name__ == "__main__":
    sys.exit(main())


# The end.
