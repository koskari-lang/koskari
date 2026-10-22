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
churn_coro_send benchmark: Koskari-only serial send churn.  A plain
Python function calls a two-argument Koskari function once per element
and sends None into each returned Koskari Coroutine, taking the result
from the StopIteration it raises.  No Python variant; runnable under
--mode koskari.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import coro_await as _ki
from registry import Benchmark


MASK32 = 0xffffffff


def churn_coro_send(n):
    mix = _ki.mix
    acc = 0
    for i in range(n):
        c = mix(i, n - i)
        try:
            c.send(None)
        except StopIteration as e:
            acc = (acc * 33 + e.value) & MASK32
    return acc


BENCHMARK = Benchmark(
    name="coro_send",
    runs=10,
    args=(2000,),
    py=None,
    ki=churn_coro_send,
    expected=2507005360,
    tags=("coroutine", "send", "koskari-only"),
)


# The end.
