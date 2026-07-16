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
pidigits game benchmark: unbounded spigot (Gibbons) for the digits of pi.
Arbitrary-precision integer arithmetic on growing integers -- a direct
exercise of the bignum path.  The assembled first-n-digits integer is a known
oracle (the leading digits of pi).

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from ki import pidigits as _ki
from registry import Benchmark


def pidigits(n):
    q, r, t, k, m, x = 1, 0, 1, 1, 3, 3
    decimal = 0
    count = 0
    while count < n:
        if 4 * q + r - t < m * t:
            decimal = decimal * 10 + m
            count = count + 1
            q, r, m = (
                10 * q,
                10 * (r - m * t),
                (10 * (3 * q + r)) // t - 10 * m,
            )
        else:
            q, r, t, k, m, x = (
                q * k,
                (2 * q + r) * x,
                t * x,
                k + 1,
                (q * (7 * k + 2) + r * x) // (t * x),
                x + 2,
            )
    return decimal


BENCHMARK = Benchmark(
    name="pidigits",
    runs=50,
    args=(50,),
    py=pidigits,
    ki=_ki.pidigits,
    expected=31415926535897932384626433832795028841971693993751,
    tags=("bignum", "game"),
)


# The end.
