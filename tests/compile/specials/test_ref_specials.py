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
Tests for koskari.compile compile/ref and compile/deref special forms.
"""


from types import SimpleNamespace

from koskari.lib import OpCode, deref

from . import SpecialsTestCase


class TestRefSpecial(SpecialsTestCase):

    def test_ref_symbol_expands_to_refq(self):
        src_str = """
        (compile/let [[x 10]]
          (compile/call deref (ref x)))
        """
        def deref(r):
            return r.deref()

        expr, mod = self.compile_expr(src_str, deref=deref)
        result = expr().run()
        self.assertEqual(10, result)

    def test_ref_attr_expands_to_ref_attr(self):
        obj = SimpleNamespace(count=5)
        src_str = """
        (compile/call deref (ref (compile/attr obj count)))
        """
        def deref(r):
            return r.deref()

        expr, mod = self.compile_expr(src_str, obj=obj, deref=deref)
        result = expr().run()
        self.assertEqual(5, result)

    def test_ampersand_dotted_expands_to_ref_attr(self):
        obj = SimpleNamespace(count=7)
        src_str = """
        (compile/call deref &obj.count)
        """
        def deref(r):
            return r.deref()

        expr, mod = self.compile_expr(src_str, obj=obj, deref=deref)
        result = expr().run()
        self.assertEqual(7, result)

    def test_ref_item_expands_to_ref_item(self):
        lst = [10, 20, 30]
        src_str = """
        (compile/call deref (ref (compile/item lst 1)))
        """
        def deref(r):
            return r.deref()

        expr, mod = self.compile_expr(src_str, lst=lst, deref=deref)
        result = expr().run()
        self.assertEqual(20, result)


class TestDerefSpecial(SpecialsTestCase):

    def test_deref_emits_deref_opcode(self):
        src_str = """
        (compile/let [[x 42]]
          (compile/let [[r (compile/refq x)]]
            (compile/deref r)))
        """
        expr, mod = self.compile_expr(src_str)
        result = expr().run()
        self.assertEqual(42, result)

    def test_deref_uses_deref_opcode(self):
        code = self.compile_to_code("""
        (compile/let [[x 1]]
          (compile/let [[r (compile/refq x)]]
            (compile/deref r)))
        """)
        ops = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(OpCode.KOS_OP_DEREF, ops)

    def test_deref_call_for_higher_order(self):
        src_str = """
        (compile/let [[a 1] [b 2]]
          (compile/let [[ra (compile/refq a)] [rb (compile/refq b)]]
            (compile/call deref ra)))
        """
        expr, mod = self.compile_expr(src_str, deref=deref)
        result = expr().run()
        self.assertEqual(1, result)
