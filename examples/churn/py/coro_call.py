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
churn_coro_call benchmark: Koskari-only async boundary churn.  A Python
coroutine calls a two-argument Koskari function once per element (each
call returns a Koskari Coroutine) and gathers the results on asyncio.  No
Python variant; runnable under --mode koskari.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio

from ki import coro_call as _ki
from registry import Benchmark


MASK32 = 0xffffffff


async def churn_coro_call(n):
    mix = _ki.mix
    results = await asyncio.gather(*[mix(i, n - i) for i in range(n)])
    acc = 0
    for v in results:
        acc = (acc * 33 + v) & MASK32
    return acc


BENCHMARK = Benchmark(
    name="coro_call",
    runs=10,
    args=(2000,),
    py=None,
    ki=churn_coro_call,
    expected=2507005360,
    tags=("coroutine", "call", "koskari-only"),
    hosted=True,
)


# The end.
