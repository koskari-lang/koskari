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
churn_starmap benchmark: Koskari-only demand-path churn (starmap + demand,
nested two deep).  No Python variant; runnable under --mode koskari.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import starmap as _ki
from registry import Benchmark


BENCHMARK = Benchmark(
    name="starmap",
    runs=50,
    args=(8, 8),
    py=None,
    ki=_ki.churn_starmap,
    expected=1148214816,
    tags=("demand", "koskari-only"),
)


# The end.
