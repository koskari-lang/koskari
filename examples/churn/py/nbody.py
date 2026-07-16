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
nbody game benchmark: float-heavy symplectic integration of the Jovian
planets (benchmarks-game n-body).  The energy is rounded to 9 places -- both
the canonical output convention and the tolerance that lets the Python and
Koskari doubles compare equal.  Every constant is written identically in the
Koskari kernel so the two share bit-identical inputs.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from math import sqrt

from ki import nbody as _ki
from registry import Benchmark


PI = 3.141592653589793
SOLAR_MASS = 4.0 * PI * PI
DAYS_PER_YEAR = 365.24


def make_bodies():
    return [
        [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, SOLAR_MASS],
        [4.84143144246472090, -1.16032004402742839, -0.103622044471123109,
         0.00166007664274403694 * DAYS_PER_YEAR,
         0.00769901118419740425 * DAYS_PER_YEAR,
         -0.0000690460016972063023 * DAYS_PER_YEAR,
         0.000954791938424326609 * SOLAR_MASS],
        [8.34336671824457987, 4.12479856412430479, -0.403523417114321381,
         -0.00276742510726862411 * DAYS_PER_YEAR,
         0.00499852801234917238 * DAYS_PER_YEAR,
         0.0000230417297573763929 * DAYS_PER_YEAR,
         0.000285885980666130812 * SOLAR_MASS],
        [12.8943695621391310, -15.1111514016986312, -0.223307578892655734,
         0.00296460137564761618 * DAYS_PER_YEAR,
         0.00237847173959480950 * DAYS_PER_YEAR,
         -0.0000296589568540237556 * DAYS_PER_YEAR,
         0.0000436624404335156298 * SOLAR_MASS],
        [15.3796971148509165, -25.9193146099879641, 0.179258772950371181,
         0.00268067772490389322 * DAYS_PER_YEAR,
         0.00162824170038242295 * DAYS_PER_YEAR,
         -0.0000951592254519715870 * DAYS_PER_YEAR,
         0.0000151027818298017794 * SOLAR_MASS],
    ]


def offset_momentum(bodies):
    px = 0.0
    py = 0.0
    pz = 0.0
    for b in bodies:
        px += b[3] * b[6]
        py += b[4] * b[6]
        pz += b[5] * b[6]
    sun = bodies[0]
    sun[3] = (0.0 - px) / SOLAR_MASS
    sun[4] = (0.0 - py) / SOLAR_MASS
    sun[5] = (0.0 - pz) / SOLAR_MASS


def advance(bodies, dt):
    n = len(bodies)
    for i in range(n):
        bi = bodies[i]
        for j in range(i + 1, n):
            bj = bodies[j]
            dx = bi[0] - bj[0]
            dy = bi[1] - bj[1]
            dz = bi[2] - bj[2]
            d2 = dx * dx + dy * dy + dz * dz
            mag = dt / (d2 * sqrt(d2))
            bim = bi[6] * mag
            bjm = bj[6] * mag
            bi[3] = bi[3] - dx * bjm
            bi[4] = bi[4] - dy * bjm
            bi[5] = bi[5] - dz * bjm
            bj[3] = bj[3] + dx * bim
            bj[4] = bj[4] + dy * bim
            bj[5] = bj[5] + dz * bim
    for b in bodies:
        b[0] = b[0] + dt * b[3]
        b[1] = b[1] + dt * b[4]
        b[2] = b[2] + dt * b[5]


def energy(bodies):
    n = len(bodies)
    e = 0.0
    for i in range(n):
        b = bodies[i]
        e += 0.5 * b[6] * (b[3] * b[3] + b[4] * b[4] + b[5] * b[5])
        for j in range(i + 1, n):
            bj = bodies[j]
            dx = b[0] - bj[0]
            dy = b[1] - bj[1]
            dz = b[2] - bj[2]
            dist = sqrt(dx * dx + dy * dy + dz * dz)
            e = e - (b[6] * bj[6]) / dist
    return e


def nbody(n):
    bodies = make_bodies()
    offset_momentum(bodies)
    step = 0
    while step < n:
        advance(bodies, 0.01)
        step += 1
    return round(energy(bodies), 9)


BENCHMARK = Benchmark(
    name="nbody",
    runs=5,
    args=(100_000,),
    py=nbody,
    ki=_ki.nbody,
    expected=-0.168140943,
    tags=("float", "game"),
)


# The end.
