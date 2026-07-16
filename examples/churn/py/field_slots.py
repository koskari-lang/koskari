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
churn_field_slots benchmark: churn_field on a __slots__ class (SLOT path).

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import field_slots as _ki
from registry import Benchmark


MASK32 = 0xFFFFFFFF


class SlotBox:

    __slots__ = ("i", "a", "b")

    def __init__(self):
        self.i = 0
        self.a = 0
        self.b = 0


def churn_field_slots(n, x=1):
    box = SlotBox()
    box.i = 0
    box.a = x & MASK32
    box.b = 1
    while box.i < n:
        box.a += box.b
        box.a &= MASK32
        box.b ^= box.a
        box.i += 1
    return (box.a ^ box.b) & MASK32


BENCHMARK = Benchmark(
    name="field_slots",
    runs=50,
    args=(1_000_000, 1),
    py=churn_field_slots,
    ki=_ki.churn_field_slots,
    tags=("attr", "slots"),
)


# The end.
