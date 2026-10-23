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
churn_starmap benchmark: itertools.starmap driving a bound method, nested
two deep, the shape of koslife's get-all-groups.  The Python variant runs
the grid class in Python; the Koskari kernel wraps each method in demand,
so the ratio is the demand boundary under starmap against a Python bound
method in the same position.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from itertools import product, starmap

from ki import starmap as _ki
from registry import Benchmark


MASK32 = 0xffffffff


class ChurnGrid:

    def __init__(self, w, h):
        self.width = w
        self.height = h
        self.wrap = False
        self.data = {}

        tmp = 175

        # seed live cells so dict.get hits both present and absent keys
        for x, y in product(range(0, w), range(0, h)):
            if 0 == (x + y) % 3:
                self.data[(x, y)] = tmp
                tmp = tmp ^ ((tmp << 5) & MASK32)

    def coord_key(self, x, y):
        w = self.width
        h = self.height
        if not self.wrap:
            if not (-1 < x < w and -1 < y < h):
                return None
        return (x % w, y % h)

    def get_cell(self, x, y):
        key = self.coord_key(x, y)
        return 0 if key is None else self.data.get(key, 0)

    def group_coord(self, x, y):
        return product((x - 1, x, x + 1), (y - 1, y, y + 1))

    def get_group(self, x, y):
        return starmap(self.get_cell, self.group_coord(x, y))

    def get_all_groups(self):
        return starmap(
            self.get_group,
            product(range(0, self.width), range(0, self.height)))


def churn_starmap(w=8, h=8):
    grid = ChurnGrid(w, h)
    acc = 0
    for grp in grid.get_all_groups():
        for v in grp:
            if v:
                acc = (acc + v) & MASK32
    return acc


BENCHMARK = Benchmark(
    name="starmap",
    runs=50,
    args=(8, 8),
    py=churn_starmap,
    ki=_ki.churn_starmap,
    expected=1148214816,
    tags=("demand", "method"),
)


# The end.
