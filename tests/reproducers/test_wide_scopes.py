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
Scopes wider than 128 names.

Every declared name has a fast slot; 128 was once a storage limit that
left names past it homeless and wrote closure refs past the allocation.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import pytest

from tests.compile.specials import SpecialsTestCase


pytestmark = pytest.mark.phase_reproducers


def _lets(n, start=0):
    return " ".join(f"[v{i} {i}]" for i in range(start, n))


class TestWideScopes(SpecialsTestCase):

    async def test_130_let_locals(self):
        src = f"(let [{_lets(130)}] #(v0 v127 v128 v129))"
        expr, mod = self.compile_expr(src)
        self.assertEqual((0, 127, 128, 129), await expr())


    async def test_130_parameters(self):
        params = " ".join(f"p{i}" for i in range(130))
        args = " ".join(str(i) for i in range(130))
        src = f"((lambda [{params}] #(p0 p128 p129)) {args})"
        expr, mod = self.compile_expr(src)
        self.assertEqual((0, 128, 129), await expr())


    async def test_127_locals_and_closures(self):
        src = f"""
        (let [[a 1] [b 2] [c 3]]
          ((lambda [] (let [{_lets(127)}] #(a b c v126)))))
        """
        expr, mod = self.compile_expr(src)
        self.assertEqual((1, 2, 3, 126), await expr())


    async def test_200_locals_and_closures_repeated(self):
        src = f"""
        (let [[a 1] [b 2] [c 3] [d 4] [e 5]]
          (define f (lambda [] (let [{_lets(200)}] (+ a b c d e v199))))
          (for-each [i (range 500)] (f))
          (f))
        """
        expr, mod = self.compile_expr(src, range=range)
        self.assertEqual(1 + 2 + 3 + 4 + 5 + 199, await expr())


# The end.
