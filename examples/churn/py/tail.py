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
churn_tail benchmark: self tail-call churn, one call per step.

The depth stays under Python's default recursion limit, since the
Python variant pays a real frame per step where the Koskari kernel
reuses one.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import tail as _ki
from registry import Benchmark


MASK32 = 0xFFFFFFFF


def churn_tail(n, x):
    if n <= 0:
        return x
    return churn_tail(n - 1, (x * 1664525 + 1013904223) & MASK32)


BENCHMARK = Benchmark(
    name="tail",
    runs=5000,
    args=(800, 1),
    py=churn_tail,
    ki=_ki.churn_tail,
    tags=("call", "recursion"),
)


# The end.
