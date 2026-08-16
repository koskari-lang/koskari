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
Tests for koskari.compile cond, and, or special forms.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestCondSpecial(SpecialsTestCase):

    async def test_cond_empty(self):
        src_str = "(cond)"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, None)


    async def test_cond_single_true(self):
        src_str = "(cond [True 100])"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 100)


    async def test_cond_else_only(self):
        src_str = "(cond [else: 100])"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 100)


    async def test_cond_first_match(self):
        src_str = """
        (cond
          [False 100]
          [True 101]
          [else: 102])
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 101)


    async def test_cond_else_fallthrough(self):
        src_str = """
        (cond
          [False 100]
          [False 101]
          [else: 102])
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 102)


    async def test_cond_as_expression(self):
        def add(a, b):
            return a + b

        src_str = """
        (call add
          (cond [False 100] [nil 101] [else: 102])
          100)
        """
        expr, mod = self.compile_expr(src_str, add=add)
        result = await expr()
        self.assertEqual(result, 202)


    async def test_cond_nested(self):
        src_str = """
        (cond
          [False 100]
          [False 101]
          [else: (cond [False 102] [else: 103])])
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 103)


class TestAndSpecial(SpecialsTestCase):

    async def test_and_no_args(self):
        src_str = "(and)"
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), True)


    async def test_and_single_truthy(self):
        src_str = "(and 42)"
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), 42)


    async def test_and_single_falsy(self):
        src_str = "(and None)"
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), None)


    async def test_and_all_truthy_returns_last(self):
        src_str = "(and 1 2 3)"
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), 3)


    async def test_and_short_circuits_on_falsy(self):
        src_str = "(and 1 None 3)"
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), None)


    async def test_and_returns_first_falsy(self):
        src_str = "(and False 99)"
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), False)


    async def test_and_side_effects_stop_at_falsy(self):
        log = []

        def side(val):
            log.append(val)
            return val

        src_str = """
        (and
          (call side 1)
          (call side None)
          (call side 3))
        """
        expr, mod = self.compile_expr(src_str, side=side)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(log, [1, None])


class TestOrSpecial(SpecialsTestCase):

    async def test_or_no_args(self):
        src_str = "(or)"
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), False)


    async def test_or_single_truthy(self):
        src_str = "(or 42)"
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), 42)


    async def test_or_single_falsy(self):
        src_str = "(or None)"
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), None)


    async def test_or_short_circuits_on_truthy(self):
        src_str = "(or None 99 0)"
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), 99)


    async def test_or_all_falsy_returns_last(self):
        src_str = "(or None False 0)"
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), 0)


    async def test_or_returns_first_truthy(self):
        src_str = "(or False 42)"
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), 42)


    async def test_or_side_effects_stop_at_truthy(self):
        log = []

        def side(val):
            log.append(val)
            return val

        src_str = """
        (or
          (call side None)
          (call side 42)
          (call side 99))
        """
        expr, mod = self.compile_expr(src_str, side=side)
        result = await expr()
        self.assertEqual(result, 42)
        self.assertEqual(log, [None, 42])


# The end.
