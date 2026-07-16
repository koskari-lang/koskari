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
binary_trees game benchmark: allocation churn plus recursion (benchmarks-game
binary-trees).  The check sum is fully determined by the depth range, so it
doubles as a correctness oracle.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import binary_trees as _ki
from registry import Benchmark


def make_tree(d):
    if d <= 0:
        return None
    return (make_tree(d - 1), make_tree(d - 1))


def check_tree(node):
    if node is None:
        return 1
    left, right = node
    return 1 + check_tree(left) + check_tree(right)


def binary_trees(min_depth, max_depth):
    total = 0
    d = min_depth
    while d <= max_depth:
        count = 1 << ((max_depth - d) + min_depth)
        subtotal = 0
        i = 0
        while i < count:
            subtotal += check_tree(make_tree(d))
            i += 1
        total += subtotal
        d += 2
    return total


BENCHMARK = Benchmark(
    name="binary_trees",
    runs=10,
    args=(4, 10),
    py=binary_trees,
    ki=_ki.binary_trees,
    expected=129712,
    tags=("alloc", "recursion", "game"),
)


# The end.
