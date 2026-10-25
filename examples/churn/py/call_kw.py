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
churn_call_kw benchmark: call churn with a keyword argument on every
call, a loop calling a two-formal function with its second argument by
keyword.  The Python variant is the same loop over a Python function; the
Koskari kernel makes one Koskari-to-Koskari keyword call per element.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import call_kw as _ki
from registry import Benchmark


MASK32 = 0xffffffff


def mixkw(a, b=0):
    return ((a * 31) + (b ^ 0x5bd1e995)) & MASK32


def churn_call_kw(n=20000):
    acc = 0
    for i in range(0, n):
        acc = ((acc * 33) + mixkw(i, b=n - i)) & MASK32
    return acc


BENCHMARK = Benchmark(
    name="call_kw",
    runs=10,
    args=(20000,),
    py=churn_call_kw,
    ki=_ki.churn_call_kw,
    expected=1667253792,
    tags=("call", "keyword"),
)


# The end.
