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
async_tree benchmark: a fan-out tree of concurrent tasks (pyperformance
async_tree, the "none" workload).  Task creation, gather, and scheduler
round trips with no I/O.  The answer is the leaf count, branches ** levels,
so it doubles as a correctness oracle.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio

from ki import async_tree as _ki
from registry import Benchmark


async def recurse(level, branches):
    if level == 0:
        return 1
    held = [recurse(level - 1, branches) for _ in range(branches)]
    return sum(await asyncio.gather(*held))


async def async_tree(levels, branches):
    return await recurse(levels, branches)


BENCHMARK = Benchmark(
    name="async_tree",
    runs=10,
    args=(6, 4),
    py=async_tree,
    ki=_ki.async_tree,
    expected=4096,
    tags=("async", "alloc", "call"),
    hosted=True,
)


# The end.
