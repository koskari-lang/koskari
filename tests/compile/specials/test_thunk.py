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
Tests for gensym-thunk.

Sources use the same spine shape the reader emits for thunk syntax ``{...}``:
``(gensym-thunk FORM ...)`` with each FORM a sibling in the tail.
That matches deferring evaluation of those forms (e.g. ``{foo}`` as
``(gensym-thunk foo)``), not wrapping an extra call around them.
"""


import pytest

from koskari.exceptions import SpecialError
from koskari.lib import bracket_nil, bracket_pair, cons, symbol
from tests.dis.dis import dis

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestGensymPartial(SpecialsTestCase):

    async def test_unary_plus_one(self):
        src = "(gensym-thunk (+ %1 1))"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertEqual(await fn(5), 6)


    async def test_two_placeholders(self):
        src = "(gensym-thunk (* %1 %2))"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertEqual(await fn(3, 4), 12)


    async def test_duplicate_pct_same_binding(self):
        src = "(gensym-thunk (== %1 %1))"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertIs(await fn(9), True)


    async def test_gap_raises(self):
        src = "(gensym-thunk (+ %2 1))"
        with self.assertRaises(SpecialError):
            self.compile_expr(src)


    async def test_thunk(self):
        src = "(gensym-thunk 99)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertEqual(await fn(), 99)


    async def test_nested_gensym_thunk_not_rewritten(self):
        src = """
            (gensym-thunk
             (gensym-thunk (+ %1 1)))
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

        src = "(gensym-thunk (call f ...))"
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

        src = "(gensym-thunk (call f %1 ...))"
        expr, mod = self.compile_expr(src, f=spy)
        dis(expr)
        fn = await expr()
        self.assertEqual(await fn(10, 1, 2, 3), 16)
        self.assertEqual(got[0], (10, (1, 2, 3)))


class TestThunkBodyShape(SpecialsTestCase):
    """
    The rewrite touches placeholders and ... only: a dotted tail is
    kept, a placeholder tail gets its binding, and cells keep their
    delimiter style
    """

    async def test_dotted_quoted_pair(self):
        expr, mod = self.compile_expr("{ '(1 . 2) }")
        fn = await expr()
        self.assertEqual(cons(1, 2), await fn())


    async def test_dotted_placeholder_tail_as_in_a_lambda(self):
        expr, mod = self.compile_expr("{(tup . %1)}", tup=tuple)
        fn = await expr()
        expr, mod = self.compile_expr("(lambda [x] (tup . x))", tup=tuple)
        plain = await expr()
        self.assertEqual(await plain((1, 2)), await fn((1, 2)))


    async def test_dotted_quasiquote_tail(self):
        expr, mod = self.compile_expr("{`(a ,@%1 . b)}")
        fn = await expr()
        got = await fn([1, 2])
        self.assertEqual(
            cons(symbol('a'), 1, 2, symbol('b')), got)


    async def test_quoted_bracket_list_keeps_its_brackets(self):
        expr, mod = self.compile_expr("{ '[a b] }")
        fn = await expr()
        got = await fn()
        self.assertIsInstance(got, bracket_pair)
        self.assertEqual("[a b]", str(got))


    async def test_bracket_nil_stays_bracket_nil(self):
        expr, mod = self.compile_expr("{ [] }")
        fn = await expr()
        self.assertIs(bracket_nil, await fn())


    async def test_quasiquoted_bracket_with_placeholder(self):
        expr, mod = self.compile_expr("{`[a ,%1]}")
        fn = await expr()
        got = await fn(5)
        self.assertIsInstance(got, bracket_pair)
        self.assertEqual("[a 5]", str(got))


# The end.
