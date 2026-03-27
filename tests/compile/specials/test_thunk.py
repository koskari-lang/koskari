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
Tests for compile/gensym-thunk.

Sources use the same spine shape the reader emits for thunk syntax ``{...}``:
``(compile/gensym-thunk FORM ...)`` with each FORM a sibling in the tail.
That matches deferring evaluation of those forms (e.g. ``{foo}`` as
``(compile/gensym-thunk foo)``), not wrapping an extra call around them.
"""


import pytest

from koskari.dis import dis
from koskari.exceptions import SpecialError

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestGensymPartial(SpecialsTestCase):

    async def test_unary_plus_one(self):
        src = "(compile/gensym-thunk (compile/+ %1 1))"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertEqual(await fn(5), 6)


    async def test_two_placeholders(self):
        src = "(compile/gensym-thunk (compile/* %1 %2))"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertEqual(await fn(3, 4), 12)


    async def test_duplicate_pct_same_binding(self):
        src = "(compile/gensym-thunk (compile/= %1 %1))"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertIs(await fn(9), True)


    async def test_gap_raises(self):
        src = "(compile/gensym-thunk (compile/+ %2 1))"
        with self.assertRaises(SpecialError):
            self.compile_expr(src)


    async def test_thunk(self):
        src = "(compile/gensym-thunk 99)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertEqual(await fn(), 99)


    async def test_nested_gensym_thunk_not_rewritten(self):
        src = """
            (compile/gensym-thunk
             (compile/gensym-thunk (compile/+ %1 1)))
        """
        expr, mod = self.compile_expr(src)
        outer = await expr()
        inner = await outer()
        self.assertEqual(await inner(7), 8)


    async def test_variadic_rest(self):
        got = []

        def spy(*args):
            got.append(args)
            return sum(args)

        src = "(compile/gensym-thunk (compile/call f ...))"
        expr, mod = self.compile_expr(src, f=spy)
        dis(expr)
        fn = await expr()
        dis(fn)
        self.assertEqual(await fn(1, 2, 3), 6)
        self.assertEqual(got[0], (1, 2, 3))


    async def test_positional_plus_variadic(self):
        got = []

        def spy(a, *args):
            got.append((a, args))
            return a + sum(args)

        src = "(compile/gensym-thunk (compile/call f %1 ...))"
        expr, mod = self.compile_expr(src, f=spy)
        dis(expr)
        fn = await expr()
        self.assertEqual(await fn(10, 1, 2, 3), 16)
        self.assertEqual(got[0], (10, (1, 2, 3)))


# The end.
