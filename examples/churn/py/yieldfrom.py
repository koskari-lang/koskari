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
churn_yieldfrom benchmark: Koskari-only yield-from delegation churn (full and
abandoned consumption).  No Python variant; runnable under --mode koskari.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import yieldfrom as _ki
from registry import Benchmark


BENCHMARK = Benchmark(
    name="yieldfrom",
    runs=50,
    args=(64,),
    py=None,
    ki=_ki.churn_yieldfrom,
    expected=129024,
    tags=("generator", "koskari-only"),
)


# The end.
