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
churn_demand_call_param benchmark: the demand_call shape with one dynamic
binding in force, each call reading it.  The Koskari kernel maps a
demand-wrapped function under a parameterize extent; the Python variant
maps a Python function under a contextvars binding, Python's dynamic
binding, so the ratio compares the two dynamic reads as well as the
boundary.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import contextvars

from ki import demand_call_param as _ki
from registry import Benchmark


MASK32 = 0xffffffff

salt = contextvars.ContextVar("salt", default=0)


def mix(a, b):
    return ((a * 31) + (b ^ salt.get())) & MASK32


def churn_demand_call_param(n=20000):
    acc = 0
    token = salt.set(0x5bd1e995)
    try:
        for v in map(mix, range(0, n), range(n, 0, -1)):
            acc = ((acc * 33) + v) & MASK32
    finally:
        salt.reset(token)
    return acc


BENCHMARK = Benchmark(
    name="demand_call_param",
    runs=10,
    args=(20000,),
    py=churn_demand_call_param,
    ki=_ki.churn_demand_call_param,
    expected=1667253792,
    tags=("demand", "call", "parameterize"),
)


# The end.
