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
coroutines benchmark: an async fibonacci (pyperformance coroutines).  One
coroutine object per call, awaited by its caller; no scheduler involvement.
The answer is fib(n), a correctness oracle.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import coroutines as _ki
from registry import Benchmark


async def fib(n):
    if n <= 1:
        return n
    return await fib(n - 1) + await fib(n - 2)


async def coroutines(n):
    return await fib(n)


BENCHMARK = Benchmark(
    name="coroutines",
    runs=10,
    args=(25,),
    py=coroutines,
    ki=_ki.coroutines,
    expected=75025,
    tags=("async", "call", "recursion"),
    hosted=True,
)


# The end.
