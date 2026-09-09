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
Tests for koskari.compile let and letrec special forms.
"""


import pytest

from tests.dis.dis import dis

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestLetSpecial(SpecialsTestCase):

    async def test_let_single_binding(self):
        src_str = "(let [[x 42]] x)"
        expr, mod = self.compile_expr(src_str)

        dis(expr.code)

        result = await expr()
        self.assertEqual(42, result)


    async def test_let_multiple_bindings(self):
        src_str = """
        (let [[a 1] [b 2]]
          (call + a b))
        """
        expr, mod = self.compile_expr(src_str, **{"+": lambda x, y: x + y})

        dis(expr.code)

        result = await expr()
        self.assertEqual(3, result)


    async def test_let_body_sequence(self):
        src_str = """
        (let [[x 10]]
          (begin
            (call * x 2)))
        """
        expr, mod = self.compile_expr(src_str, **{"*": lambda x, y: x * y})

        dis(expr.code)

        result = await expr()
        self.assertEqual(20, result)


    async def test_named_let_invoked_lambda(self):
        # (let foo [[a 1] [b 2]] ...body) -> ((lambda foo [a b] ...body) 1 2)
        src_str = """
        (let foo [[a 10] [b 20]]
          (call + a b))
        """
        expr, mod = self.compile_expr(src_str, **{"+": lambda x, y: x + y})
        result = await expr()
        self.assertEqual(30, result)


class TestLetrecSpecial(SpecialsTestCase):

    async def test_letrec_rhs_sees_prior_binding_left_to_right(self):
        # Second binding RHS runs after first VAR_STORE; b resolves to a's value.
        src_str = """
        (letrec [[a 1] [b a]]
          (call + a b))
        """
        expr, mod = self.compile_expr(src_str, **{"+": lambda x, y: x + y})
        result = await expr()
        self.assertEqual(2, result)

    async def test_letrec_closure_calls_bound_lambda(self):
        src_str = """
        (letrec [[f (lambda [] 99)]]
          (call f))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(99, result)

    async def test_letrec_recursive_factorial(self):
        src_str = """
        (letrec [[fact (lambda [n]
                          (cond
                            [(== n 0) 1]
                            [else:
                              (* n
                                (call fact (- n 1)))]))]]
          (call fact 5))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(120, result)


# The end.
