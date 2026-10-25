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
churn_demand_call benchmark: builtins.map calling a two-argument function
once per element.  The Python variant maps a Python function; the Koskari
kernel maps the function wrapped in demand, one Python-to-Koskari boundary
call per element, so the ratio is the cost of that boundary against a
Python callee in the same position.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import demand_call as _ki
from registry import Benchmark


MASK32 = 0xffffffff


def mix(a, b):
    return ((a * 31) + (b ^ 0x5bd1e995)) & MASK32


def churn_demand_call(n=20000):
    acc = 0
    for v in map(mix, range(0, n), range(n, 0, -1)):
        acc = ((acc * 33) + v) & MASK32
    return acc


BENCHMARK = Benchmark(
    name="demand_call",
    runs=10,
    args=(20000,),
    py=churn_demand_call,
    ki=_ki.churn_demand_call,
    expected=1667253792,
    tags=("demand", "call"),
)


# The end.
