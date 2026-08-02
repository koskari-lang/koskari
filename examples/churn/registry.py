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
Benchmark descriptor and discovery for the churn harness.

A benchmark is one ``py/<name>.py`` module -- which owns the descriptor and,
when present, the Python variant -- paired with one ``ki/<name>.ki`` kernel.
Discovery imports every non-underscore module in the :mod:`py` package and
reads its module-level ``BENCHMARK``, so registering a benchmark touches no
central list.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import dataclasses
import importlib
import pkgutil
import typing


@dataclasses.dataclass
class Benchmark:
    """
    One churn benchmark: a Koskari kernel, an optional Python variant to
    compare it against, and the parameters that drive them.

    ``py`` is None for a Koskari-only case (nothing to compare against;
    still runnable under ``--mode koskari`` for tracing).  ``expected``, when
    set, is a known result the answer is checked against -- so a Koskari-only
    run stays correctness-checked with no Python side present.
    """

    name: str
    runs: int
    args: typing.Tuple
    ki: typing.Callable
    py: typing.Optional[typing.Callable] = None
    expected: typing.Optional[object] = None
    tags: typing.Tuple = ()


def discover():
    """
    Import every case module in the ``py`` package and collect its
    ``BENCHMARK`` descriptor, keyed by benchmark name.  Underscore-prefixed
    modules are treated as private helpers and skipped.
    """

    import py as cases

    found = {}
    for info in pkgutil.iter_modules(cases.__path__):
        if info.name.startswith("_"):
            continue
        try:
            module = importlib.import_module(f"py.{info.name}")
        except SyntaxError:
            # a reference kernel using newer-Python syntax (for example
            # match_case's match statement, 3.10+); skip it on older
            # interpreters rather than breaking discovery
            continue
        bench = module.BENCHMARK
        found[bench.name] = bench
    return found


# The end.
