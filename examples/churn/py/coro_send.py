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
churn_coro_send benchmark: serial send churn.  A plain function calls a
two-argument function once per element and sends None into each returned
coroutine, taking the result from the StopIteration it raises.  The
Python variant sends into a Python async def coroutine; the Koskari
kernel sends into the Koskari Coroutine each call returns.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import coro_await as _ki
from py.coro_await import mix
from registry import Benchmark


MASK32 = 0xffffffff


def driver(mix):
    """
    The send loop over one mix callable, shared by both variants
    """

    def churn_coro_send(n):
        acc = 0
        for i in range(n):
            c = mix(i, n - i)
            try:
                c.send(None)
            except StopIteration as e:
                acc = (acc * 33 + e.value) & MASK32
        return acc
    return churn_coro_send


BENCHMARK = Benchmark(
    name="coro_send",
    runs=10,
    args=(2000,),
    py=driver(mix),
    ki=driver(_ki.mix),
    expected=2507005360,
    tags=("coroutine", "send"),
)


# The end.
