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
Tests for the evaluate and compile runtime functions from basics.ki

evaluate must accept a source form, a Code object, or an already
compiled Expression, and return the value of running it in the given
namespace. Requires import koskari.builtins so the functions and the
compile/evaluate macro are present.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from koskari.compile import Compiler
from koskari.lib import Code, Expression, Module, ModuleNamespace
from koskari.reader import default_reader, source_str

import pytest
import unittest


pytestmark = pytest.mark.phase_basics


class TestEvaluate(unittest.IsolatedAsyncioTestCase):

    def compile_expr(self, src_str, lineno=1, **defns):
        import koskari.builtins as builtins
        bltns = vars(builtins)
        if defns:
            bltns = dict(bltns)
            bltns.update(defns)

        mod_ns = ModuleNamespace("test", bltns)
        module = Module(mod_ns)
        source = default_reader.read(
            source_str(src_str, __file__, line_no=lineno))
        compiler = Compiler("<unittest>", module, filename=__file__)
        mod_ns['__compiler__'] = compiler
        fragment = compiler.compile_expression(source)
        return fragment.as_expression(mod_ns), module


    async def test_evaluate_macro_one_argument(self):
        """
        (evaluate form) compiles and runs the form in (locals)
        """

        expr, _mod = self.compile_expr("(evaluate (+ 1 2))")
        self.assertEqual(await expr(), 3)


    async def test_evaluate_macro_explicit_namespace(self):
        """
        (evaluate form ns) runs the form in the given namespace
        """

        src_str = """
        (evaluate (+ x 2) (locals))
        """
        expr, _mod = self.compile_expr(src_str, x=40)
        self.assertEqual(await expr(), 42)


    async def test_evaluate_call_with_quoted_source(self):
        """
        the evaluate function takes a source object and a namespace
        """

        src_str = """
        (call evaluate '(* 6 7) (locals))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), 42)


    async def test_evaluate_call_with_code(self):
        """
        a Code object is turned into an expression in the namespace
        """

        src_str = """
        (call evaluate (attr (compileq (* 6 7)) code) (locals))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), 42)


    async def test_evaluate_call_with_expression(self):
        """
        an Expression is invoked as-is
        """

        src_str = """
        (call evaluate (compileq (* 6 7)) (locals))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), 42)


    async def test_compileq_returns_expression(self):
        """
        compile returns an Expression whose code is a Code object
        """

        src_str = """
        (compileq (+ 1 2))
        """
        expr, _mod = self.compile_expr(src_str)
        compiled = await expr()
        self.assertIsInstance(compiled, Expression)
        self.assertIsInstance(compiled.code, Code)


# The end.
