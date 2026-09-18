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
async_generators benchmark: an async generator consumed by async for
(pyperformance async_generators).  The answer is the sum of range(n), a
correctness oracle.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import async_generators as _ki
from registry import Benchmark


async def produce(n):
    for i in range(n):
        yield i


async def async_generators(n):
    total = 0
    async for i in produce(n):
        total += i
    return total


BENCHMARK = Benchmark(
    name="async_generators",
    runs=50,
    args=(20000,),
    py=async_generators,
    ki=_ki.async_generators,
    expected=199990000,
    tags=("async", "generator", "iteration"),
    hosted=True,
)


# The end.
