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
churn_tree benchmark: recursive call-depth churn (two self-calls per frame).

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import tree as _ki
from registry import Benchmark


MASK32 = 0xFFFFFFFF


def churn_tree(depth, x=1):
    if depth <= 0:
        x = (x * 1664525 + 1013904223) & MASK32
        x ^= (x << 13) & MASK32
        x ^= (x >> 17)
        x ^= (x << 5) & MASK32
        return x & MASK32

    a = churn_tree(depth - 1, (x * 1664525 + 1013904223) & MASK32)
    b = churn_tree(depth - 1, (x ^ 0x9E3779B9) & MASK32)
    return ((a + ((b ^ x) * 33)) ^ ((a << 7) & MASK32) ^ (b >> 3)) & MASK32


BENCHMARK = Benchmark(
    name="tree",
    runs=50,
    args=(18, 50),
    py=churn_tree,
    ki=_ki.churn_tree,
    tags=("call", "recursion"),
)


# The end.
