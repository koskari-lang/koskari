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

The Koskari kernel opens a reset whose body never shifts. The Python
variant is a try block that never raises, with Escape the exception
shift_abort raises through the same block.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import reset_plain as _ki
from registry import Benchmark


MASK32 = 0xFFFFFFFF


class Escape(Exception):
    pass


def churn_reset_plain(n, x):
    i = 0
    while i < n:
        try:
            x = (x * 1664525 + 1013904223) & MASK32
        except Escape as esc:
            x = esc.args[0]
        i += 1
    return x


BENCHMARK = Benchmark(
    name="reset_plain",
    runs=50,
    args=(20000, 1),
    py=churn_reset_plain,
    ki=_ki.churn_reset_plain,
    tags=("continuation",),
)


# The end.
