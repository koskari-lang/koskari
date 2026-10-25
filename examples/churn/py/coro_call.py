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
churn_coro_call benchmark: async fan-out churn.  One driver coroutine
calls a two-argument function once per element and gathers the returned
coroutines on asyncio.  The Python variant gathers Python async def
coroutines; the Koskari kernel gathers the Koskari Coroutine each call
returns, so the ratio is the coroutine boundary against a Python
coroutine in the same position.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio

from ki import coro_call as _ki
from registry import Benchmark


MASK32 = 0xffffffff


async def mix(a, b):
    return ((a * 31) + (b ^ 0x5bd1e995)) & MASK32


def driver(mix):
    """
    The gather loop over one mix callable, shared by both variants
    """

    async def churn_coro_call(n):
        results = await asyncio.gather(*[mix(i, n - i) for i in range(n)])
        acc = 0
        for v in results:
            acc = (acc * 33 + v) & MASK32
        return acc
    return churn_coro_call


BENCHMARK = Benchmark(
    name="coro_call",
    runs=10,
    args=(2000,),
    py=driver(mix),
    ki=driver(_ki.mix),
    expected=2507005360,
    tags=("coroutine", "call"),
    hosted=True,
)


# The end.
