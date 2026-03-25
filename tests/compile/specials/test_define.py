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
Tests for koskari.compile compile/define special form.
"""


from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestDefineDeclareOnly(SpecialsTestCase):

    async def test_define_then_setq_then_read(self):
        src_str = """
        (compile/begin
          (define x)
          (compile/setq x 42)
          x)
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(42, result)


class TestDefineWithExpr(SpecialsTestCase):

    async def test_define_then_read(self):
        src_str = """
        (compile/begin
          (define y 99)
          y)
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(99, result)


class TestDefineFunctionShorthand(SpecialsTestCase):

    async def test_define_double(self):
        src_str = """
        (compile/begin
          (define double [x] (mul x 2))
          (double 5))
        """
        expr, mod = self.compile_expr(src_str, mul=lambda a, b: a * b)
        result = await expr()
        self.assertEqual(10, result)


    async def test_define_fact(self):
        src_str = """
        (compile/begin
          (define fact [n]
            (cond
              [(eq n 0) 1]
              [else: (mul n (fact (sub n 1)))]))
          (fact 5))
        """
        expr, mod = self.compile_expr(
            src_str,
            eq=lambda a, b: a == b,
            mul=lambda a, b: a * b,
            sub=lambda a, b: a - b)
        result = await expr()
        self.assertEqual(120, result)


    async def test_define_function_shorthand_inline_return_annotation(self):
        src_str = """
        (compile/begin
          (define f :: int [x] (mul x 2))
          f)
        """
        expr, mod = self.compile_expr(src_str, mul=lambda a, b: a * b)
        f = await expr()
        self.assertEqual(f.__annotations__['return'], 'int')
        self.assertEqual(await f(5), 10)


    async def test_define_function_shorthand_decl_bracket_still_works(self):
        src_str = """
        (compile/begin
          (define [f :: int] [x] (mul x 2))
          f)
        """
        expr, mod = self.compile_expr(src_str, mul=lambda a, b: a * b)
        f = await expr()
        self.assertEqual(f.__annotations__['return'], 'int')
        self.assertEqual(await f(5), 10)


    async def test_define_function_shorthand_type_colon_return_annotation(self):
        src_str = """
        (compile/begin
          (define g type: int [x] (mul x 2))
          g)
        """
        expr, mod = self.compile_expr(src_str, mul=lambda a, b: a * b)
        g = await expr()
        self.assertEqual(g.__annotations__['return'], 'int')
        self.assertEqual(await g(5), 10)


class TestDefineInlineTypedValue(SpecialsTestCase):

    async def test_define_inline_typed_value(self):
        src_str = """
        (compile/begin
          (define z :: int 100)
          z)
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(100, result)


# The end.
