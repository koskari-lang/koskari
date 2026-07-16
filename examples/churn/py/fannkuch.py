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
fannkuch game benchmark: fannkuch-redux (benchmarks-game).  Integer array
indexing and in-place mutation in tight loops.  Returns (max_flips, checksum);
for n=7 that is the known (16, 228).

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import fannkuch as _ki
from registry import Benchmark


def fannkuch(n):
    perm1 = list(range(n))
    count = [0 for _ in range(n)]
    max_flips = 0
    checksum = 0
    perm_count = 0
    r = n

    while True:
        while r != 1:
            count[r - 1] = r
            r = r - 1

        perm = list(perm1)
        flips = 0
        k = perm[0]
        while k != 0:
            i = 0
            j = k
            while i < j:
                tmp = perm[i]
                perm[i] = perm[j]
                perm[j] = tmp
                i = i + 1
                j = j - 1
            flips = flips + 1
            k = perm[0]

        if flips > max_flips:
            max_flips = flips
        if perm_count % 2 == 0:
            checksum = checksum + flips
        else:
            checksum = checksum - flips

        advanced = False
        while not advanced:
            if r == n:
                return (max_flips, checksum)
            perm0 = perm1[0]
            i2 = 0
            while i2 < r:
                j2 = i2 + 1
                perm1[i2] = perm1[j2]
                i2 = j2
            perm1[r] = perm0
            count[r] = count[r] - 1
            if count[r] > 0:
                advanced = True
            else:
                r = r + 1
        perm_count = perm_count + 1


BENCHMARK = Benchmark(
    name="fannkuch",
    runs=50,
    args=(7,),
    py=fannkuch,
    ki=_ki.fannkuch,
    expected=(16, 228),
    tags=("array", "game"),
)


# The end.
