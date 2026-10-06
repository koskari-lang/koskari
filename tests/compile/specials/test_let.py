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

from koskari.exceptions import CompilerError
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


class TestLetInitScope(SpecialsTestCase):
    """
    A let's init expressions run before its scope opens, in the
    enclosing scope, and they compile there too
    """

    async def test_init_lambda_cannot_write_a_let_name(self):
        src_str = """
        (lambda []
          (let [[x 0]
                [set-x (lambda [] (setq x 5))]]
            (set-x)
            x))
        """
        with self.assertRaises(CompilerError) as caught:
            self.compile_expr(src_str)
        self.assertIn("'x'", str(caught.exception))

    async def test_init_reads_the_enclosing_binding(self):
        src_str = """
        (begin
          (define x 1)
          ((lambda []
             (let [[x 2] [y x]]
               y))))
        """
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(1, await expr())


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


class TestInlineScopeClosures(SpecialsTestCase):
    """
    A function constant built inside a let or class body within a
    function reads and writes the enclosing function's locals, not a
    module global of the same name
    """

    async def test_lambda_in_let_reads_the_function_local(self):
        src_str = """
        (begin
          (define a "module")
          ((lambda [a]
             (let [[b 1]]
               ((lambda [] a))))
           "argument"))
        """
        expr, mod = self.compile_expr(src_str)
        self.assertEqual("argument", await expr())


    async def test_lambda_in_let_writes_the_function_local(self):
        src_str = """
        (begin
          (define a "module")
          (define f
            (lambda [a]
              (let [[b 1]]
                ((lambda [] (setq a "written")))
                a)))
          #[(f "argument") a])
        """
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(["written", "module"], await expr())


    async def test_lambda_in_nested_lets_reads_the_function_local(self):
        src_str = """
        (begin
          (define a "module")
          ((lambda [a]
             (let [[b 1]]
               (let [[c 2]]
                 ((lambda [] a)))))
           "argument"))
        """
        expr, mod = self.compile_expr(src_str)
        self.assertEqual("argument", await expr())


    async def test_lambda_in_letrec_reads_the_function_local(self):
        src_str = """
        (begin
          (define a "module")
          ((lambda [a]
             (letrec [[get (lambda [] a)]]
               (get)))
           "argument"))
        """
        expr, mod = self.compile_expr(src_str)
        self.assertEqual("argument", await expr())


    async def test_method_in_class_body_reads_the_function_local(self):
        src_str = """
        (begin
          (define a "module")
          ((lambda [a]
             (define K
               (class K ()
                 (define get (lambda [self] a))))
             ((attr (K) get)))
           "argument"))
        """
        expr, mod = self.compile_expr(src_str)
        self.assertEqual("argument", await expr())


    async def test_method_in_class_body_names_its_own_class(self):
        src_str = """
        (begin
          ((lambda []
             (define K
               (class K ()
                 (define make (lambda [self] (K)))))
             (type ((attr (K) make))))))
        """
        expr, mod = self.compile_expr(src_str, type=type)
        self.assertEqual("K", (await expr()).__name__)
