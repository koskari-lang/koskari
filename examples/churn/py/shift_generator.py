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
churn_shift_generator benchmark: a producer suspended and resumed once
per value

The Koskari kernel shifts each value out with the continuation that
goes on to the next, and the consumer calls it once. The Python variant
is a generator stepped with next. Python resumes the one generator
frame where Koskari resumes a copy of the captured region, so the ratio
measures that difference.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import shift_generator as _ki
from registry import Benchmark


MASK32 = 0xFFFFFFFF


def produce(items):
    for v in items:
        yield v


def churn_shift_generator(n):
    data = list(range(n))
    acc = 0
    gen = produce(data)
    step = next(gen, None)
    while step is not None:
        acc = (acc * 31 + step) & MASK32
        step = next(gen, None)
    return acc


BENCHMARK = Benchmark(
    name="shift_generator",
    runs=500,
    args=(2000,),
    py=churn_shift_generator,
    ki=_ki.churn_shift_generator,
    tags=("continuation", "generator"),
)


# The end.
