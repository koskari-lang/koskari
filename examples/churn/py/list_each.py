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
churn_list_each benchmark: build a list by comprehension, then fold it.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import list_each as _ki
from registry import Benchmark


MASK32 = 0xFFFFFFFF


def churn_step(x, i):
    x = (x * 1664525 + 1013904223) & MASK32
    x ^= (x << 13) & MASK32
    x ^= (x >> 17)
    x ^= (x << 5) & MASK32
    return (x + i * 1103515245) & MASK32


def churn_list_each(n, x=1):
    acc = 0
    for v in [churn_step(x, i) for i in range(n)]:
        acc ^= v
    return acc & MASK32


BENCHMARK = Benchmark(
    name="list_each",
    runs=50,
    args=(1_000_000, 1),
    py=churn_list_each,
    ki=_ki.churn_list_each,
    tags=("iteration", "alloc"),
)


# The end.
