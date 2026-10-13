# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
Tests for the compile/var, compile/global, and compile/build-class
special forms.
"""


import pytest

from koskari.exceptions import SyntaxMatchError

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestVar(SpecialsTestCase):

    async def test_var_value(self):
        expr, mod = self.compile_expr("(begin (var y 99) y)")
        self.assertEqual(99, await expr())


    async def test_var_declare_then_setq(self):
        expr, mod = self.compile_expr("(begin (var x) (setq x 42) x)")
        self.assertEqual(42, await expr())


    async def test_var_result_is_none(self):
        expr, mod = self.compile_expr("(var z 1)")
        self.assertIsNone(await expr())


    async def test_var_typed_value(self):
        expr, mod = self.compile_expr("(begin (var n :: int 7) n)")
        self.assertEqual(7, await expr())


    async def test_var_type_keyword_value(self):
        expr, mod = self.compile_expr("(begin (var n type: int 8) n)")
        self.assertEqual(8, await expr())


    async def test_var_typed_declare(self):
        expr, mod = self.compile_expr(
            "(begin (var n :: int) (setq n 3) n)")
        self.assertEqual(3, await expr())


    async def test_var_binds_in_function_scope(self):
        src = """
        (begin
          (var f (lambda [] (var inner 5) inner))
          (f))
        """
        expr, mod = self.compile_expr(src)
        self.assertEqual(5, await expr())
        self.assertNotIn("inner", vars(mod))


    async def test_var_no_function_shorthand(self):
        with self.assertRaises(SyntaxMatchError):
            self.compile_expr("(var f [x] x)")


class TestGlobal(SpecialsTestCase):

    async def test_global_value(self):
        expr, mod = self.compile_expr("(begin (global g 5) g)")
        self.assertEqual(5, await expr())
        self.assertEqual(5, vars(mod)["g"])


    async def test_global_declare_then_setq(self):
        expr, mod = self.compile_expr("(begin (global g) (setq g 6) g)")
        self.assertEqual(6, await expr())


    async def test_global_typed_value(self):
        expr, mod = self.compile_expr("(begin (global g :: int 9) g)")
        self.assertEqual(9, await expr())


    async def test_global_from_function_body(self):
        src = """
        (begin
          (var f (lambda [] (global counter 10) None))
          (f)
          counter)
        """
        expr, mod = self.compile_expr(src)
        self.assertEqual(10, await expr())
        self.assertEqual(10, vars(mod)["counter"])


class TestBuildClass(SpecialsTestCase):

    async def test_build_class_binds_nothing(self):
        src = """
        (begin
          (var K (build-class Unbound () (var k 1)))
          K)
        """
        expr, mod = self.compile_expr(src)
        cls = await expr()
        self.assertIsInstance(cls, type)
        self.assertEqual("Unbound", cls.__name__)
        self.assertEqual(1, cls.k)
        self.assertNotIn("Unbound", vars(mod))


# The end.
