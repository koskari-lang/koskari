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
churn_wind_plain benchmark: one dyn-wind per step with empty segments,
entered and left with no transfer across it.  Koskari only: Python has no
such extent.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import wind_plain as _ki
from registry import Benchmark


BENCHMARK = Benchmark(
    name="wind_plain",
    runs=100,
    args=(20000,),
    ki=_ki.churn_wind_plain,
    expected=199990000,
    tags=("wind",),
    perf=True,
)


# The end.
