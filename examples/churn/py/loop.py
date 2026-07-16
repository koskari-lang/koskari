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
churn_loop benchmark: a tight integer-mixing while loop.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import loop as _ki
from registry import Benchmark


MASK32 = 0xFFFFFFFF


def churn_loop(n, x=1):
    i = 0
    while i < n:
        x = (x * 1664525 + 1013904223) & MASK32
        x ^= (x << 13) & MASK32
        x ^= (x >> 17)
        x ^= (x << 5) & MASK32
        x = (x + i * 1103515245) & MASK32
        i += 1
    return x


BENCHMARK = Benchmark(
    name="loop",
    runs=50,
    args=(1_000_000, 1),
    py=churn_loop,
    ki=_ki.churn_loop,
    tags=("loop", "dispatch"),
)


# The end.
