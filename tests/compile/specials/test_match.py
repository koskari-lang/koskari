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
Tests for the koskari.compile match special form

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestMatchSpecial(SpecialsTestCase):

    async def test_match_no_clauses(self):
        expr, mod = self.compile_expr("(compile/match 5)")
        self.assertEqual(await expr(), None)


    async def test_match_else_only(self):
        expr, mod = self.compile_expr("(compile/match 5 [else: 100])")
        self.assertEqual(await expr(), 100)


    async def test_match_scalar_literal(self):
        src = """
        (compile/match 5
          [1 "one"]
          [5 "five"]
          [else: "meh"])
        """
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), "five")


    async def test_match_scalar_no_match_to_else(self):
        src = """
        (compile/match 9
          [1 "one"]
          [5 "five"]
          [else: "meh"])
        """
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), "meh")


    async def test_match_sequence_binds(self):
        src = """
        (compile/match data
          [[a b c] b]
          [else: 0])
        """
        expr, mod = self.compile_expr(src, data=(1, 2, 3))
        self.assertEqual(await expr(), 2)


    async def test_match_sequence_wrong_length_falls_through(self):
        src = """
        (compile/match data
          [[a b] b]
          [else: 99])
        """
        expr, mod = self.compile_expr(src, data=(1, 2, 3))
        self.assertEqual(await expr(), 99)


    async def test_match_sequence_rest(self):
        src = """
        (compile/match data
          [[a *: rest] a]
          [else: 0])
        """
        expr, mod = self.compile_expr(src, data=(1, 2, 3))
        self.assertEqual(await expr(), 1)


    async def test_match_bare_symbol_captures_whole(self):
        src = """
        (compile/match data
          [x x])
        """
        expr, mod = self.compile_expr(src, data=42)
        self.assertEqual(await expr(), 42)


    async def test_match_typed_extern(self):
        src = """
        (compile/match data
          [(x :: int) x]
          [else: "nope"])
        """
        expr, mod = self.compile_expr(src, data=7, int=int)
        self.assertEqual(await expr(), 7)


    async def test_match_typed_extern_no_match(self):
        src = """
        (compile/match data
          [(x :: int) x]
          [else: "nope"])
        """
        expr, mod = self.compile_expr(src, data="hello", int=int)
        self.assertEqual(await expr(), "nope")


    async def test_match_first_arm_wins(self):
        src = """
        (compile/match data
          [[a b] "two"]
          [[a b c] "three"]
          [else: "other"])
        """
        expr, mod = self.compile_expr(src, data=(1, 2))
        self.assertEqual(await expr(), "two")


# The end.
