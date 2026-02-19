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
Tests for koskari.compile cond, if, when, unless special forms.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from . import SpecialsTestCase


class TestCondSpecial(SpecialsTestCase):

    def test_cond_empty(self):
        src_str = "(compile/cond)"
        expr, mod = self.compile_expr(src_str)
        result = expr().run()
        self.assertEqual(result, None)


    def test_cond_single_true(self):
        src_str = "(compile/cond [True 100])"
        expr, mod = self.compile_expr(src_str)
        result = expr().run()
        self.assertEqual(result, 100)


    def test_cond_else_only(self):
        src_str = "(compile/cond [else: 100])"
        expr, mod = self.compile_expr(src_str)
        result = expr().run()
        self.assertEqual(result, 100)


    def test_cond_first_match(self):
        src_str = """
        (compile/cond
          [False 100]
          [True 101]
          [else: 102])
        """
        expr, mod = self.compile_expr(src_str)
        result = expr().run()
        self.assertEqual(result, 101)


    def test_cond_else_fallthrough(self):
        src_str = """
        (compile/cond
          [False 100]
          [False 101]
          [else: 102])
        """
        expr, mod = self.compile_expr(src_str)
        result = expr().run()
        self.assertEqual(result, 102)


    def test_cond_as_expression(self):
        def add(a, b):
            return a + b

        src_str = """
        (compile/call add
          (compile/cond [False 100] [nil 101] [else: 102])
          100)
        """
        expr, mod = self.compile_expr(src_str, add=add)
        result = expr().run()
        self.assertEqual(result, 202)


    def test_cond_nested(self):
        src_str = """
        (compile/cond
          [False 100]
          [False 101]
          [else: (compile/cond [False 102] [else: 103])])
        """
        expr, mod = self.compile_expr(src_str)
        result = expr().run()
        self.assertEqual(result, 103)


    def test_if_then_else(self):
        src_str_t = "(compile/if True 1 2)"
        expr, mod = self.compile_expr(src_str_t)
        self.assertEqual(expr().run(), 1)

        src_str_f = "(compile/if False 1 2)"
        expr, mod = self.compile_expr(src_str_f)
        self.assertEqual(expr().run(), 2)


    def test_when(self):
        src_str_t = "(compile/when True 42)"
        expr, mod = self.compile_expr(src_str_t)
        self.assertEqual(expr().run(), 42)

        src_str_f = "(compile/when False 42)"
        expr, mod = self.compile_expr(src_str_f)
        self.assertEqual(expr().run(), None)


    def test_unless(self):
        src_str_f = "(compile/unless False 42)"
        expr, mod = self.compile_expr(src_str_f)
        self.assertEqual(expr().run(), 42)

        src_str_t = "(compile/unless True 42)"
        expr, mod = self.compile_expr(src_str_t)
        self.assertEqual(expr().run(), None)


# The end.
