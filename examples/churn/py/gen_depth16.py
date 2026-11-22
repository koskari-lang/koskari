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
churn_gen_depth16 benchmark: a generator stepped by for-each while its
yield sits inside 16 nested let scopes.  Koskari only: Python has no
scope node to nest.  The three gen_depth benchmarks share one kernel module
and differ only in the nesting, so their counts compare directly.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import gen_depth as _ki
from registry import Benchmark


BENCHMARK = Benchmark(
    name="gen_depth16",
    runs=400,
    args=(20000,),
    ki=_ki.churn_gen_depth16,
    expected=199990000,
    tags=("generator",),
)


# The end.
