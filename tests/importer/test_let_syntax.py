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
Tests for let-syntax and inline-macro from koskari.builtins.

Requires import koskari.builtins so bootstrap definitions are loaded.
"""


import pytest
import unittest

from koskari.compile import Compiler
from koskari.lib import Module, ModuleNamespace
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class BasicsCompileHelpers(unittest.IsolatedAsyncioTestCase):

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
        fragment = compiler.full_compile(source)
        return fragment.as_expression(mod_ns), module


class TestLetSyntax(BasicsCompileHelpers):

    async def test_let_syntax_local_syntax_rules(self):
        src_str = """
        (let-syntax twice
          (syntax-rules []
            [(twice x) (+ x x)])
          (twice 5))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(10, result)


    async def test_let_syntax_nested_shadow(self):
        src_str = """
        (let-syntax twice
          (syntax-rules []
            [(twice x) (+ x 1)])
          (let-syntax twice
            (syntax-rules []
              [(twice x) (+ x x)])
            (twice 3)))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(6, result)


class TestInlineMacro(BasicsCompileHelpers):

    async def test_inline_macro_splices_source(self):
        src_str = """
        (inline-macro
          (quote (+ 10 32)))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(42, result)


    async def test_inline_macro_body_yielding_none_expands_to_none(self):
        """
        A body that evaluates to None expands to the literal None, so the
        form is a value and not a hole in the bytecode
        """

        src_str = """
        (begin
          (inline-macro None)
          (inline-macro (begin))
          #[(inline-macro None) 1 (inline-macro (begin))])
        """
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), [None, 1, None])


    async def test_inline_macro_keeps_falsy_expansions(self):
        """
        A body that evaluates to a falsy value expands to that value
        """

        src_str = """
        #[(inline-macro 0) (inline-macro False) (inline-macro "")]
        """
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), [0, False, ""])


class TestMacroRulesNoneExpansion(BasicsCompileHelpers):

    async def test_macro_rules_returning_none_expands_to_none(self):
        """
        A macro-rules arm whose body returns None expands to the literal
        None wherever the macro is used
        """

        src_str = """
        (let-syntax nothing
          (macro-rules []
            [(nothing) None])
          #[(nothing) 2 (nothing)])
        """
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), [None, 2, None])

    async def test_macro_rules_falsy_form_kept(self):
        src_str = """
        (let-syntax zero
          (macro-rules []
            [(zero) 0])
          #[(zero) (zero)])
        """
        expr, mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), [0, 0])


# The end.
