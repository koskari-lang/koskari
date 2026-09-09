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
Tests for the compile/flet macro from _functions.ki

flet binds local functions for the extent of a body. Each binding is an
anonymous function whose initializer is evaluated in the enclosing scope,
so a binding shadows an outer function of the same name but does not see
its siblings (no mutual recursion). Exercises nested-ellipsis template
expansion end to end: the per-binding body is captured under two ``...``.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile import Compiler
from koskari.exceptions import CompilerError
from koskari.lib import Module, ModuleNamespace
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class TestFletMacro(unittest.IsolatedAsyncioTestCase):
    """
    Tests for compile/flet. Requires import koskari.builtins so the macro
    is present for expansion.
    """

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
        fragment = compiler.compile_expression(source)
        return fragment.as_expression(mod_ns), module


    async def test_flet_single_binding(self):
        """
        One local function is bound and invoked
        """

        src_str = """
        (flet [[double [x] (* x 2)]]
          (double 21))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(42, await expr())


    async def test_flet_multiple_bindings(self):
        """
        Several local functions coexist and compose over the body
        """

        src_str = """
        (flet [[double [x] (* x 2)]
               [inc [x] (+ x 1)]]
          (double (inc 20)))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(42, await expr())


    async def test_flet_multi_form_body_binding(self):
        """
        A binding with a multi-form body runs each form; the last is its
        value. This drives the nested-ellipsis path (``fbody ...`` under the
        binding-list ``...``) with more than one captured body form.
        """

        src_str = """
        (let [[acc 0]]
          (flet [[stash [x]
                   (setq acc x)
                   (* acc 3)]]
            (stash 4)))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(12, await expr())

    async def test_flet_body_cannot_write_a_sibling_let_binding(self):
        """
        A flet binding's lambda is made in the enclosing scope, so a let
        opened in the flet body does not enclose it; a write to that
        let's name from the lambda is undeclared and refused at compile
        time instead of reaching a module global
        """

        src_str = """
        (flet [[stash [x]
                 (setq acc x)
                 (* acc 3)]]
          (let [[acc 0]]
            (stash 4)))
        """
        with self.assertRaises(CompilerError) as caught:
            self.compile_expr(src_str)
        self.assertIn("acc", str(caught.exception))


    async def test_flet_shadows_outer_without_forward_reference(self):
        """
        Initializers evaluate in the enclosing scope: a sibling reference
        resolves to the outer binding, not the flet sibling. Here ``g`` calls
        the outer ``f`` (x * 10), never the flet ``f`` (x + 1).
        """

        src_str = """
        (let [[f (lambda [x] (* x 10))]]
          (flet [[f [x] (+ x 1)]
                 [g [x] (f x)]]
            (g 5)))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(50, await expr())


    async def test_flet_body_sees_binding(self):
        """
        The flet body itself sees the local bindings (shadowing works for
        the body even though sibling initializers do not)
        """

        src_str = """
        (let [[f (lambda [x] (* x 10))]]
          (flet [[f [x] (+ x 1)]]
            (f 5)))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(6, await expr())


# The end.
