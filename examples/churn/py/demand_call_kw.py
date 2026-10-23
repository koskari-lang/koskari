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
churn_demand_call_kw benchmark: builtins.map over functools.partial binding
a keyword of a two-formal function, one call per element each carrying the
keyword.  The Python variant binds a Python function; the Koskari kernel
binds the function wrapped in demand, one Python-to-Koskari boundary call
per element.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from functools import partial

from ki import demand_call_kw as _ki
from registry import Benchmark


MASK32 = 0xffffffff


def mixkw(a, b=0):
    return ((a * 31) + (b ^ 0x5bd1e995)) & MASK32


def churn_demand_call_kw(n=20000):
    acc = 0
    for v in map(partial(mixkw, b=7), range(0, n)):
        acc = ((acc * 33) + v) & MASK32
    return acc


BENCHMARK = Benchmark(
    name="demand_call_kw",
    runs=10,
    args=(20000,),
    py=churn_demand_call_kw,
    ki=_ki.churn_demand_call_kw,
    expected=1714684240,
    tags=("demand", "call", "keyword"),
)


# The end.
