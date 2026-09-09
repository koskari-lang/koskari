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
# along with this library; if not, see
# <http://www.gnu.org/licenses/>.

"""
tests.reproducers.test_formals_bracket_default

Reproducer: bracket formal defaults like [[a 1]] must not appear in both
pos and kwd lists.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3

Equivalent to (define foo [a: 1] a) but written as (define foo [[a 1]] a).
Bracket (name default) is syntactic sugar for keyword default name: default.
"""


import pytest

from tests.compile.specials import SpecialsTestCase


pytestmark = pytest.mark.phase_reproducers


class TestFormalsBracketDefault(SpecialsTestCase):


    async def test_bracket_default_callable_without_args(self):
        """
        (lambda [[a 1]] a) must accept zero args and use the default
        """

        src = "(lambda [[a 1]] a)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertIsNone(fn.formals.pos)
        self.assertEqual(fn.formals.kwd, ("a",))
        self.assertEqual(await fn(), 1)
        self.assertEqual(await fn(a=2), 2)


    async def test_required_positional_plus_bracket_default(self):
        """
        (lambda [x [y 10]] y) must treat [y 10] as keyword default only
        """

        src = "(lambda [x [y 10]] y)"
        expr, mod = self.compile_expr(src)
        fn = await expr()
        self.assertEqual(fn.formals.pos, ("x",))
        self.assertEqual(fn.formals.kwd, ("y",))
        self.assertEqual(await fn(1), 10)
        self.assertEqual(await fn(2, y=3), 3)


# The end.
