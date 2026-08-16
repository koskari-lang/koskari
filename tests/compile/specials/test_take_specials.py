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
Tests for koskari.compile takeq, take-item, and
take-attr special forms.
"""


from types import SimpleNamespace

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestTakeqSpecial(SpecialsTestCase):

    async def test_takeq_returns_value_and_clears(self):
        src_str = """
        (begin
          (setq z 42)
          (takeq z))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 42)

        glbls = vars(mod)
        refz = glbls.ref("z")
        self.assertTrue(refz.empty)


    async def test_takeq_on_uninitialized_raises(self):
        src_str = """
        (begin
          (define x)
          (takeq x))
        """
        expr, mod = self.compile_expr(src_str)
        with self.assertRaises(NameError):
            await expr()


    async def test_delq_on_uninitialized_returns_none(self):
        src_str = """
        (begin
          (define x)
          (delq x))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertIsNone(result)


class TestTakeItemSpecial(SpecialsTestCase):

    async def test_take_item_returns_and_removes(self):
        data = {"k": 5}
        src_str = '(take-item d "k")'
        expr, mod = self.compile_expr(src_str, d=data)
        result = await expr()
        self.assertEqual(result, 5)
        self.assertNotIn("k", data)


class TestTakeAttrSpecial(SpecialsTestCase):

    async def test_take_attr_returns_and_removes(self):
        obj = SimpleNamespace(v=11)
        src_str = "(take-attr obj v)"
        expr, mod = self.compile_expr(src_str, obj=obj)
        result = await expr()
        self.assertEqual(result, 11)
        self.assertFalse(hasattr(obj, "v"))


# The end.
