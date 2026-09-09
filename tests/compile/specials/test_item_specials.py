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
Tests for koskari.compile item, del-item, and set-item
special forms.
"""


import pytest

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestItemSpecial(SpecialsTestCase):

    async def test_item_constant_key(self):
        lst = [10, 20, 30]
        src_str = "(item lst 0)"
        expr, mod = self.compile_expr(src_str, lst=lst)
        result = await expr()
        self.assertEqual(10, result)


    async def test_item_variable_key(self):
        lst = [10, 20, 30]
        src_str = """
        (let [[i 2]]
          (item lst i))
        """
        expr, mod = self.compile_expr(src_str, lst=lst)
        result = await expr()
        self.assertEqual(30, result)


class TestSetItemSpecial(SpecialsTestCase):

    async def test_set_item_then_read(self):
        lst = [1, 2, 3]
        src_str = """
        (begin
          (set-item lst 1 99)
          (item lst 1))
        """
        expr, mod = self.compile_expr(src_str, lst=lst)
        result = await expr()
        self.assertEqual(99, result)
        self.assertEqual(99, lst[1])


class TestDelItemSpecial(SpecialsTestCase):

    async def test_del_item_returns_none(self):
        d = {"remove_me": 1}
        src_str = '(del-item d "remove_me")'
        expr, mod = self.compile_expr(src_str, d=d)
        result = await expr()
        self.assertIsNone(result)
        self.assertNotIn("remove_me", d)


# The end.
