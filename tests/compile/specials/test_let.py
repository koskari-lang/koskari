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
Tests for koskari.compile compile/let and compile/letrec special forms.
"""


from tests.dis.dis import dis
from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestLetSpecial(SpecialsTestCase):

    async def test_let_single_binding(self):
        src_str = "(compile/let [[x 42]] x)"
        expr, mod = self.compile_expr(src_str)

        dis(expr.code)

        result = await expr()
        self.assertEqual(42, result)


    async def test_let_multiple_bindings(self):
        src_str = """
        (compile/let [[a 1] [b 2]]
          (compile/call + a b))
        """
        expr, mod = self.compile_expr(src_str, **{"+": lambda x, y: x + y})

        dis(expr.code)

        result = await expr()
        self.assertEqual(3, result)


    async def test_let_body_sequence(self):
        src_str = """
        (compile/let [[x 10]]
          (compile/begin
            (compile/call * x 2)))
        """
        expr, mod = self.compile_expr(src_str, **{"*": lambda x, y: x * y})

        dis(expr.code)

        result = await expr()
        self.assertEqual(20, result)


    async def test_named_let_invoked_lambda(self):
        # (let foo [[a 1] [b 2]] ...body) -> ((lambda foo [a b] ...body) 1 2)
        src_str = """
        (compile/let foo [[a 10] [b 20]]
          (compile/call + a b))
        """
        expr, mod = self.compile_expr(src_str, **{"+": lambda x, y: x + y})
        result = await expr()
        self.assertEqual(30, result)


class TestLetrecSpecial(SpecialsTestCase):

    async def test_letrec_rhs_sees_prior_binding_left_to_right(self):
        # Second binding RHS runs after first VAR_STORE; b resolves to a's value.
        src_str = """
        (compile/letrec [[a 1] [b a]]
          (compile/call + a b))
        """
        expr, mod = self.compile_expr(src_str, **{"+": lambda x, y: x + y})
        result = await expr()
        self.assertEqual(2, result)

    async def test_letrec_closure_calls_bound_lambda(self):
        src_str = """
        (compile/letrec [[f (compile/lambda [] 99)]]
          (compile/call f))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(99, result)

    async def test_letrec_recursive_factorial(self):
        src_str = """
        (compile/letrec [[fact (compile/lambda [n]
                          (compile/cond
                            [(compile/== n 0) 1]
                            [else:
                              (compile/* n
                                (compile/call fact (compile/- n 1)))]))]]
          (compile/call fact 5))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(120, result)


# The end.
