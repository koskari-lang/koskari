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
churn_gen_try benchmark: a generator stepped by a for loop whose yield sits
in the handler of a try, so the handled exception is live across every
step.  The Python variant is the same generator and loop.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import gen_try as _ki
from registry import Benchmark


MASK32 = 0xffffffff


def handled(n):
    try:
        raise ValueError("held")
    except ValueError:
        for i in range(n):
            yield i


def churn_gen_try(n):
    acc = 0
    for v in handled(n):
        acc = (acc + v) & MASK32
    return acc


BENCHMARK = Benchmark(
    name="gen_try",
    runs=400,
    args=(20000,),
    py=churn_gen_try,
    ki=_ki.churn_gen_try,
    expected=199990000,
    tags=("generator",),
)


# The end.
