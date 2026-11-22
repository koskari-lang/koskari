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
churn_shift_abort benchmark: one early exit per step, through one call

The kernel shifts and drops the continuation, so the value leaves
through the reset.  Perf only: Python has no continuation to drop.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import shift_abort as _ki
from registry import Benchmark


BENCHMARK = Benchmark(
    name="shift_abort",
    runs=50,
    args=(20000, 1),
    ki=_ki.churn_shift_abort,
    expected=1453904673,
    tags=("continuation",),
    perf=True,
)


# The end.
