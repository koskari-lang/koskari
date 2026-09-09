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
Tests for the compile/labels macro from _functions.ki

labels binds local functions for the extent of a body, with the names
visible to one another so the functions can be mutually recursive. Each
function is defined inside a shared scope; this is the distinguishing
property from flet, whose initializers cannot see their siblings.
Exercises nested-ellipsis template expansion: the per-binding body is
captured under two ``...``.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile import Compiler
from koskari.lib import Module, ModuleNamespace
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class TestLabelsMacro(unittest.IsolatedAsyncioTestCase):
    """
    Tests for compile/labels. Requires import koskari.builtins so the macro
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


    async def test_labels_single_binding(self):
        """
        One local function is bound and invoked
        """

        src_str = """
        (labels [[double [x] (* x 2)]]
          (double 21))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(42, await expr())


    async def test_labels_self_recursion(self):
        """
        A binding can call itself
        """

        src_str = """
        (labels [[fact [n] (if (== n 0) 1 (* n (fact (- n 1))))]]
          (fact 5))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(120, await expr())


    async def test_labels_mutual_recursion(self):
        """
        Sibling bindings see one another: even?/odd? are mutually recursive
        """

        header = """
        (labels [[even? [n] (if (== n 0) True (odd? (- n 1)))]
                 [odd? [n] (if (== n 0) False (even? (- n 1)))]]
        """
        for body, want in (("(even? 10)", True),
                           ("(odd? 7)", True),
                           ("(even? 3)", False)):
            expr, _mod = self.compile_expr(header + body + ")")
            self.assertEqual(want, await expr())


    async def test_labels_multi_form_body_binding(self):
        """
        A binding with a multi-form body runs each form; the last is its
        value. Drives the nested-ellipsis path (``fbody ...`` under the
        binding-list ``...``) with more than one captured body form.
        """

        src_str = """
        (let [[acc 0]]
          (labels [[stash [x]
                     (setq acc x)
                     (* acc 3)]]
            (stash 4)))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(12, await expr())


    async def test_labels_forward_reference_across_siblings(self):
        """
        A binding may reference a sibling that is declared later in the same
        labels form (forward reference), because all are defined in a shared
        scope before the body runs.
        """

        src_str = """
        (labels [[caller [n] (callee n)]
                 [callee [n] (* n 100)]]
          (caller 3))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(300, await expr())


# The end.
