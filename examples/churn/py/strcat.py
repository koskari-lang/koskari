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
churn_strcat benchmark: a string built by + over three pieces and += on
one accumulator, emptied every 1024 steps.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import strcat as _ki
from registry import Benchmark


def churn_strcat(n):
    total = 0
    s = ""
    i = 0
    while i < n:
        s += "k" + str(i & 0xFF) + ","
        if (i & 0x3FF) == 0x3FF:
            total += len(s)
            s = ""
        i += 1
    return total + len(s)


BENCHMARK = Benchmark(
    name="strcat",
    runs=50,
    args=(1_000_000,),
    py=churn_strcat,
    ki=_ki.churn_strcat,
    tags=("str",),
)


# The end.
