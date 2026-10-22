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
churn_demand_call_param benchmark: Koskari-only Python-to-Koskari boundary
churn (builtins.map over a demand-wrapped two-argument function) with one
parameterize binding in force.  No Python variant; runnable under
--mode koskari.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import demand_call_param as _ki
from registry import Benchmark


BENCHMARK = Benchmark(
    name="demand_call_param",
    runs=10,
    args=(20000,),
    py=None,
    ki=_ki.churn_demand_call_param,
    expected=1667253792,
    tags=("demand", "call", "parameterize", "koskari-only"),
)


# The end.
