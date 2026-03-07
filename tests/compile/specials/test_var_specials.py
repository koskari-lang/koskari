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
Tests for koskari.compile compile/setq, compile/refq, compile/bindq,
compile/delq special forms.

bindq binds a name to a Ref instance for future resolution; it does not
store a raw value.
"""


from types import SimpleNamespace

from . import SpecialsTestCase


class TestSetqSpecial(SpecialsTestCase):

    async def test_setq_then_read(self):
        src_str = """
        (compile/let [[x 0]]
          (compile/begin
            (compile/setq x 42)
            x))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(42, result)


class TestRefqSpecial(SpecialsTestCase):

    async def test_refq_deref(self):
        def deref(r):
            return r.deref()

        src_str = """
        (compile/let [[x 10]]
          (compile/call deref (compile/refq x)))
        """
        expr, mod = self.compile_expr(src_str, deref=deref)
        result = await expr()
        self.assertEqual(10, result)


class TestBindqSpecial(SpecialsTestCase):

    async def test_bindq_then_read(self):
        src_str = """
        (compile/begin
          (compile/bindq y (compile/ref-attr obj value))
          y)
        """
        obj = SimpleNamespace(value=99)
        expr, mod = self.compile_expr(src_str, obj=obj)
        result = await expr()
        self.assertEqual(99, result)


class TestDelqSpecial(SpecialsTestCase):

    async def test_delq_returns_none(self):
        src_str = """
        (compile/let [[z 1]]
          (compile/delq z))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertIsNone(result)


# The end.
