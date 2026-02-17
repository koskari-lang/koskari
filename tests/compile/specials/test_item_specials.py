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
Tests for koskari.compile compile/item, compile/ref-item, compile/del-item,
compile/set-item special forms.
"""


from . import SpecialsTestCase


class TestItemSpecial(SpecialsTestCase):

    def test_item_constant_key(self):
        lst = [10, 20, 30]
        src_str = "(compile/item lst 0)"
        expr, mod = self.compile_expr(src_str, lst=lst)
        result = expr().run()
        self.assertEqual(10, result)


    def test_item_variable_key(self):
        lst = [10, 20, 30]
        src_str = """
        (compile/let [[i 2]]
          (compile/item lst i))
        """
        expr, mod = self.compile_expr(src_str, lst=lst)
        result = expr().run()
        self.assertEqual(30, result)


class TestSetItemSpecial(SpecialsTestCase):

    def test_set_item_then_read(self):
        lst = [1, 2, 3]
        src_str = """
        (compile/begin
          (compile/set-item lst 1 99)
          (compile/item lst 1))
        """
        expr, mod = self.compile_expr(src_str, lst=lst)
        result = expr().run()
        self.assertEqual(99, result)
        self.assertEqual(99, lst[1])


class TestRefItemSpecial(SpecialsTestCase):

    def test_ref_item_deref(self):
        def deref(r):
            return r.deref()

        d = {"k": "v"}
        src_str = """
        (compile/call deref (compile/ref-item d "k"))
        """
        expr, mod = self.compile_expr(src_str, d=d, deref=deref)
        result = expr().run()
        self.assertEqual("v", result)


class TestDelItemSpecial(SpecialsTestCase):

    def test_del_item_returns_none(self):
        d = {"remove_me": 1}
        src_str = '(compile/del-item d "remove_me")'
        expr, mod = self.compile_expr(src_str, d=d)
        result = expr().run()
        self.assertIsNone(result)
        self.assertNotIn("remove_me", d)


# The end.
