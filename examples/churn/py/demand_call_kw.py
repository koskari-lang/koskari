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
churn_demand_call_kw benchmark: Koskari-only Python-to-Koskari boundary
churn with a keyword argument on every call (builtins.map over
functools.partial binding a keyword of a demand-wrapped function).  No
Python variant; runnable under --mode koskari.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import demand_call_kw as _ki
from registry import Benchmark


BENCHMARK = Benchmark(
    name="demand_call_kw",
    runs=10,
    args=(20000,),
    py=None,
    ki=_ki.churn_demand_call_kw,
    expected=1714684240,
    tags=("demand", "call", "keyword", "koskari-only"),
)


# The end.
