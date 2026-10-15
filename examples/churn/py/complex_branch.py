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
churn_complex_branch benchmark: and/or/not in every test position, a
loop test, four if/elif arms over compares and bare variables, and a
short-circuit in value position.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import complex_branch as _ki
from registry import Benchmark


MASK32 = 0xFFFFFFFF
SENTINEL = 0xDEADBEEF


def churn_complex_branch(n, x=1):
    acc = 0
    v = x & MASK32
    flag = 0
    i = 0
    while i < n and acc != SENTINEL:
        v = (v * 1103515245 + i) & MASK32
        lo = v & 0xFF
        hi = v >> 24
        if lo < 64 and hi < 64:
            acc = (acc + lo + hi) & MASK32
        elif lo == 255 or hi == 255:
            acc = (acc ^ v) & MASK32
        elif (flag or v & 1) and lo < hi:
            acc = (acc + (v >> 3)) & MASK32
            flag = 0
        elif not (lo > 128 and hi > 128):
            acc = (acc - lo) & MASK32
            flag = 1
        else:
            acc = (acc + (flag or lo)) & MASK32
        i += 1
    return acc


BENCHMARK = Benchmark(
    name="complex_branch",
    runs=50,
    args=(1_000_000, 1),
    py=churn_complex_branch,
    ki=_ki.churn_complex_branch,
    tags=("branch", "dispatch"),
)


# The end.
