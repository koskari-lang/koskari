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
Tests for koskari.compile compile/attr, compile/del-attr, compile/set-attr,
compile/ref-attr special forms.
"""


from types import SimpleNamespace

from . import SpecialsTestCase


class TestAttrSpecial(SpecialsTestCase):

    def test_attr_loads_attribute(self):
        obj = SimpleNamespace(value=10)
        src_str = "(compile/attr obj value)"
        expr, mod = self.compile_expr(src_str, obj=obj)
        result = expr().run()
        self.assertEqual(10, result)


class TestSetAttrSpecial(SpecialsTestCase):

    def test_set_attr_then_read(self):
        obj = SimpleNamespace(x=0)
        src_str = """
        (compile/begin
          (compile/set-attr obj x 42)
          (compile/attr obj x))
        """
        expr, mod = self.compile_expr(src_str, obj=obj)
        result = expr().run()
        self.assertEqual(42, result)
        self.assertEqual(42, obj.x)


class TestRefAttrSpecial(SpecialsTestCase):

    def test_ref_attr_deref(self):
        def deref(r):
            return r.deref()

        obj = SimpleNamespace(count=5)
        src_str = """
        (compile/call deref (compile/ref-attr obj count))
        """
        expr, mod = self.compile_expr(src_str, obj=obj, deref=deref)
        result = expr().run()
        self.assertEqual(5, result)


class TestDelAttrSpecial(SpecialsTestCase):

    def test_del_attr_returns_none(self):
        obj = SimpleNamespace(temporary=100)
        src_str = "(compile/del-attr obj temporary)"
        expr, mod = self.compile_expr(src_str, obj=obj)
        result = expr().run()
        self.assertIsNone(result)
        self.assertFalse(hasattr(obj, "temporary"))


# The end.
