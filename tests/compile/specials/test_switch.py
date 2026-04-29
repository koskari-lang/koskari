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
Tests for koskari.compile switch special form

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestSwitchSpecial(SpecialsTestCase):

    async def test_switch_no_clauses(self):
        src_str = "(compile/switch 5)"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, None)


    async def test_switch_else_only(self):
        src_str = "(compile/switch 5 [else: 100])"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 100)


    async def test_switch_first_match(self):
        src_str = """
        (compile/switch 5
          [1 "one"]
          [2 "two"]
          [5 "five"]
          [else: "meh"])
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, "five")


    async def test_switch_else_fallthrough(self):
        src_str = """
        (compile/switch 5
          [1 "one"]
          [2 "two"]
          [else: "meh"])
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, "meh")


    async def test_switch_no_match_returns_none(self):
        src_str = """
        (compile/switch 5
          [1 "one"]
          [2 "two"])
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, None)


    async def test_switch_value_evaluated_once(self):
        log = []

        def get_val():
            log.append("call")
            return 5

        src_str = """
        (compile/switch (compile/call get_val)
          [1 "one"]
          [2 "two"]
          [3 "three"]
          [else: "meh"])
        """
        expr, mod = self.compile_expr(src_str, get_val=get_val)
        result = await expr()
        self.assertEqual(result, "meh")
        self.assertEqual(log, ["call"])


    async def test_switch_keys_not_evaluated_after_match(self):
        log = []

        def side(val):
            log.append(val)
            return val

        src_str = """
        (compile/switch 1
          [(compile/call side 1) "one"]
          [(compile/call side 2) "two"]
          [else: "meh"])
        """
        expr, mod = self.compile_expr(src_str, side=side)
        result = await expr()
        self.assertEqual(result, "one")
        self.assertEqual(log, [1])


    async def test_switch_as_expression(self):
        def add(a, b):
            return a + b

        src_str = """
        (compile/call add
          (compile/switch 2 [1 "one"] [2 100] [else: 0])
          100)
        """
        expr, mod = self.compile_expr(src_str, add=add)
        result = await expr()
        self.assertEqual(result, 200)


class TestSwitchLayoutIntegration(SpecialsTestCase):
    """
    End-to-end tests exercising layout/switch -> compile/switch through
    the LayoutReader-driven compile_expr helper
    """

    async def test_switch_layout_first_match(self):
        src_str = """
        switch 5:
          case 1:
            "one"
          case 5:
            "five"
          else:
            "meh"
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, "five")


    async def test_switch_layout_else_fallthrough(self):
        src_str = """
        switch 5:
          case 1:
            "one"
          case 2:
            "two"
          else:
            "meh"
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, "meh")


    async def test_switch_layout_no_match_returns_none(self):
        src_str = """
        switch 5:
          case 1:
            "one"
          case 2:
            "two"
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, None)


    async def test_switch_layout_value_evaluated_once(self):
        log = []

        def get_val():
            log.append("call")
            return 5

        src_str = """
        switch (get_val):
          case 1:
            "one"
          case 2:
            "two"
          case 3:
            "three"
          else:
            "meh"
        """
        expr, mod = self.compile_expr(src_str, get_val=get_val)
        result = await expr()
        self.assertEqual(result, "meh")
        self.assertEqual(log, ["call"])


# The end.
