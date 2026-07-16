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
churn_branch benchmark: four-arm if/elif/else density per iteration.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import branch as _ki
from registry import Benchmark


MASK32 = 0xFFFFFFFF


def churn_branch(n, x=1):
    acc = 0
    v = x & MASK32
    for i in range(n):
        v = (v + i) & MASK32
        if v % 3 == 0:
            acc = (acc + (v >> 2)) & MASK32
        elif v % 5 == 0:
            acc = (acc ^ ((v << 1) & MASK32)) & MASK32
        elif v & 1:
            acc = (acc + v * 3 + 1) & MASK32
        else:
            acc = (acc + (v >> 1)) & MASK32
    return acc & MASK32


BENCHMARK = Benchmark(
    name="branch",
    runs=50,
    args=(1_000_000, 1),
    py=churn_branch,
    ki=_ki.churn_branch,
    tags=("branch", "dispatch"),
)


# The end.
