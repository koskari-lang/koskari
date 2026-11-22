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
churn_raise_unwind benchmark: one raise per step, k frames below the try
that handles it.  The Python variant is the same recursion and handler.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import raise_unwind as _ki
from registry import Benchmark


def thrower(k):
    if k == 0:
        raise ValueError("deep")
    return 1 + thrower(k - 1)


def churn_raise_unwind(n, k):
    acc = 0
    i = 0
    while i < n:
        try:
            thrower(k)
        except ValueError:
            acc += 1
        i += 1
    return acc


BENCHMARK = Benchmark(
    name="raise_unwind",
    runs=50,
    args=(2000, 16),
    py=churn_raise_unwind,
    ki=_ki.churn_raise_unwind,
    expected=2000,
    tags=("exception",),
)


# The end.
