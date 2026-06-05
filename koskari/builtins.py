# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
koskari.builtins

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


# creates the actual module and stores it in sys.modules and
# on the koskari package.
def _setup():
    from .bootstrap import make_builtins_demand

    glbls = globals()
    ns = vars(make_builtins_demand())

    for key, val in ns.items():
        if key.startswith('__'):
            continue

        try:
            glbls[key] = val.deref()
        except Exception:
            pass

    del glbls["_setup"]


_setup()


# The end.
