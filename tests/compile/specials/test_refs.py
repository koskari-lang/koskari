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
Tests for koskari.compile deref, ref-attr, ref-item,
refq, and bindq special forms.
"""


from types import SimpleNamespace

from koskari.lib import OpCode, deref

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestDerefSpecial(SpecialsTestCase):

    async def test_deref_emits_deref_opcode(self):
        src_str = """
        (let [[x 42]]
          (let [[r (refq x)]]
            (deref r)))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(42, result)


    async def test_deref_uses_deref_opcode(self):
        code = self.compile_to_code("""
        (let [[x 1]]
          (let [[r (refq x)]]
            (deref r)))
        """)
        ops = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(OpCode.DEREF, ops)


    async def test_deref_call_for_higher_order(self):
        src_str = """
        (let [[a 1] [b 2]]
          (let [[ra (refq a)] [rb (refq b)]]
            (call deref ra)))
        """
        expr, mod = self.compile_expr(src_str, deref=deref)
        result = await expr()
        self.assertEqual(1, result)


class TestRefAttrSpecial(SpecialsTestCase):

    async def test_ref_attr_deref(self):
        def deref(r):
            return r.deref()

        obj = SimpleNamespace(count=5)
        src_str = """
        (call deref (ref-attr obj count))
        """
        expr, mod = self.compile_expr(src_str, obj=obj, deref=deref)
        result = await expr()
        self.assertEqual(5, result)


class TestRefItemSpecial(SpecialsTestCase):

    async def test_ref_item_deref(self):
        def deref(r):
            return r.deref()

        d = {"k": "v"}
        src_str = """
        (call deref (ref-item d "k"))
        """
        expr, mod = self.compile_expr(src_str, d=d, deref=deref)
        result = await expr()
        self.assertEqual("v", result)


class TestRefqSpecial(SpecialsTestCase):

    async def test_refq_deref(self):
        def deref(r):
            return r.deref()

        src_str = """
        (let [[x 10]]
          (call deref (refq x)))
        """
        expr, mod = self.compile_expr(src_str, deref=deref)
        result = await expr()
        self.assertEqual(10, result)


class TestBindqSpecial(SpecialsTestCase):

    async def test_bindq_then_read(self):
        src_str = """
        (begin
          (bindq y (ref-attr obj value))
          y)
        """
        obj = SimpleNamespace(value=99)
        expr, mod = self.compile_expr(src_str, obj=obj)
        result = await expr()
        self.assertEqual(99, result)


# The end.
