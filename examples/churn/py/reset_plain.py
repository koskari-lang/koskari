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
churn_reset_plain benchmark: one delimiter per step that nothing escapes
to

The kernel opens a reset whose body never shifts.  Perf only: Python has
no delimiter to open.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import reset_plain as _ki
from registry import Benchmark


BENCHMARK = Benchmark(
    name="reset_plain",
    runs=50,
    args=(20000, 1),
    ki=_ki.churn_reset_plain,
    expected=1453904673,
    tags=("continuation",),
    perf=True,
)


# The end.
