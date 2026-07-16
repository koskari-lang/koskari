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
churn_method benchmark: one bound-method call per iteration.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import method as _ki
from registry import Benchmark


MASK32 = 0xFFFFFFFF


class ChurnCounter:

    def __init__(self):
        self.total = 0

    def bump(self, v):
        self.total = (self.total + v) & MASK32
        return self.total


def churn_method(n, x=1):
    c = ChurnCounter()
    i = 0
    while i < n:
        x = c.bump(x ^ i)
        i += 1
    return c.total


BENCHMARK = Benchmark(
    name="method",
    runs=50,
    args=(1_000_000, 1),
    py=churn_method,
    ki=_ki.churn_method,
    tags=("call", "attr", "dispatch"),
)


# The end.
