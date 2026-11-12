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
churn_shift_amb benchmark: a three-level search for Pythagorean triples

The Koskari kernel makes each choice with a shift whose handler calls
the continuation once per choice. The Python variant is the same search
as three nested loops.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import shift_amb as _ki
from registry import Benchmark


def churn_shift_amb(n):
    out = []
    for a in range(1, n):
        for b in range(a, n):
            for c in range(b, n):
                if a * a + b * b == c * c:
                    out.append((a, b, c))
    return out


BENCHMARK = Benchmark(
    name="shift_amb",
    runs=200,
    args=(21,),
    py=churn_shift_amb,
    ki=_ki.churn_shift_amb,
    expected=[
        (3, 4, 5), (5, 12, 13), (6, 8, 10),
        (8, 15, 17), (9, 12, 15), (12, 16, 20)],
    tags=("continuation",),
)


# The end.
