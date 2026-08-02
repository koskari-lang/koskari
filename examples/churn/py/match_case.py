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
churn_match_case benchmark: eight-arm structural match per iteration
over a rotating mix of subjects -- class patterns, sequence
destructuring with rest, mapping lookup, guarded and typed captures,
string atoms, and an else arm. The Python side uses the native match
statement, so answers cross-check koskari's match semantics against
PEP 634 directly.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from dataclasses import dataclass

from ki import match_case as _ki
from registry import Benchmark


MASK32 = 0xFFFFFFFF


@dataclass
class Point:
    x: int = 0
    y: int = 0


SUBJECTS = (
    Point(0, 0),
    Point(3, 4),
    (10, 20, 30, 40),
    (1, 2),
    {"k": 9, "extra": True},
    17,
    2,
    "hello",
    {"other": 1},
)


def churn_match_case(n):
    acc = 0
    count = len(SUBJECTS)
    for i in range(n):
        v = SUBJECTS[i % count]
        match v:
            case Point(x=0, y=0):
                r = 1
            case Point(x=a, y=b):
                r = a + b
            case (a, b, *rest):
                r = a * 2 + b + len(rest)
            case {"k": m}:
                r = m
            case int() as m if m > 5:
                r = m >> 1
            case int() as m:
                r = m
            case str() as s:
                r = len(s)
            case _:
                r = 7
        acc = (acc + r + i) & MASK32
    return acc


BENCHMARK = Benchmark(
    name="match_case",
    runs=20,
    args=(100_000,),
    py=churn_match_case,
    ki=_ki.churn_match_case,
    tags=("match", "branch", "dispatch"),
)


# The end.
