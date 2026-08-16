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
Tests for koskari.compile attr, del-attr, and set-attr
special forms.
"""


from types import SimpleNamespace

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestAttrSpecial(SpecialsTestCase):

    async def test_attr_loads_attribute(self):
        obj = SimpleNamespace(value=10)
        src_str = "(attr obj value)"
        expr, mod = self.compile_expr(src_str, obj=obj)
        result = await expr()
        self.assertEqual(10, result)


class TestSetAttrSpecial(SpecialsTestCase):

    async def test_set_attr_then_read(self):
        obj = SimpleNamespace(x=0)
        src_str = """
        (begin
          (set-attr obj x 42)
          (attr obj x))
        """
        expr, mod = self.compile_expr(src_str, obj=obj)
        result = await expr()
        self.assertEqual(42, result)
        self.assertEqual(42, obj.x)


class TestDelAttrSpecial(SpecialsTestCase):

    async def test_del_attr_returns_none(self):
        obj = SimpleNamespace(temporary=100)
        src_str = "(del-attr obj temporary)"
        expr, mod = self.compile_expr(src_str, obj=obj)
        result = await expr()
        self.assertIsNone(result)
        self.assertFalse(hasattr(obj, "temporary"))


class TestDottedFormExpansion(SpecialsTestCase):

    async def test_dotted_symbol_loads_attribute(self):
        foo = SimpleNamespace(bar=42)
        src_str = "foo.bar"
        expr, mod = self.compile_expr(src_str, foo=foo)
        result = await expr()
        self.assertEqual(42, result)


    async def test_dotted_symbol_multi_level(self):
        obj = SimpleNamespace(foo=SimpleNamespace(bar=99))
        src_str = "obj.foo.bar"
        expr, mod = self.compile_expr(src_str, obj=obj)
        result = await expr()
        self.assertEqual(99, result)


    async def test_dotted_form_as_call(self):
        foo = SimpleNamespace(bar=lambda a, b: a + b)
        src_str = "(foo.bar 1 2)"
        expr, mod = self.compile_expr(src_str, foo=foo)
        result = await expr()
        self.assertEqual(3, result)


    async def test_simple_dotted_no_split(self):
        foo = 100
        src_str = "foo"
        expr, mod = self.compile_expr(src_str, foo=foo)
        result = await expr()
        self.assertEqual(100, result)


# The end.
