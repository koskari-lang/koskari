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
churn_yieldfrom benchmark: yield-from delegation consumed both fully and
abandoned after one value, which exercises the delegating generator's
teardown.  The Python variant is the same pair of loops over a yield from
generator.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import yieldfrom as _ki
from registry import Benchmark


MASK32 = 0xffffffff


def yield_from_items(items):
    yield from items


def churn_yieldfrom(n=64):
    data = list(range(n))
    acc = 0

    # full consumption
    for _ in range(n):
        for v in yield_from_items(data):
            acc = (acc + v) & MASK32

    # partial consumption: abandon each generator after one value
    for _ in range(n):
        g = yield_from_items(data)
        for v in g:
            acc = (acc ^ v) & MASK32
            break
    return acc & MASK32


BENCHMARK = Benchmark(
    name="yieldfrom",
    runs=50,
    args=(64,),
    py=churn_yieldfrom,
    ki=_ki.churn_yieldfrom,
    expected=129024,
    tags=("generator",),
)


# The end.
