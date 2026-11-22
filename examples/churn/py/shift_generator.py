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

The kernel shifts each value out with the continuation that goes on to
the next, and the consumer calls it once.  Perf only: a Python generator
resumes its one frame where this resumes a copy of the captured region.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import shift_generator as _ki
from registry import Benchmark


BENCHMARK = Benchmark(
    name="shift_generator",
    runs=500,
    args=(2000,),
    ki=_ki.churn_shift_generator,
    expected=765768680,
    tags=("continuation", "generator"),
    perf=True,
)


# The end.
