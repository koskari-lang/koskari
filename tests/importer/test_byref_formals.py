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
tests.importer.test_byref_formals

By-ref formals: a formal marked by-ref aliases the caller-supplied Ref, so
set! in the callee writes through to the caller's binding. Covers the
equivalent surface shapes (&x, (compile/ref x), (ref x), [x byref: True]),
the by_refs spec field, and end-to-end write-through. Runs at phase_basics
because the call site uses the & / set! macros from bootstrap + basics.ki.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-opus-4-8
"""


import unittest

import pytest

from koskari.compile import Compiler
from koskari.lib import Module, ModuleNamespace
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class TestByRefFormals(unittest.IsolatedAsyncioTestCase):


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


    async def test_amp_marks_positional_by_ref(self):
        """
        &result is a required positional formal recorded in by_refs
        """

        expr, mod = self.compile_expr("(lambda [a &result] result)")
        fn = await expr()
        self.assertEqual(fn.formals.pos, ("a", "result"))
        self.assertEqual(fn.formals.by_refs, ("result",))


    async def test_surface_shapes_agree(self):
        """
        &x, (compile/ref x), and (ref x) produce the same by-ref formal
        """

        for src in ("(lambda [a &result] result)",
                    "(lambda [a (compile/ref result)] result)",
                    "(lambda [a (ref result)] result)"):
            expr, mod = self.compile_expr(src)
            fn = await expr()
            self.assertEqual(fn.formals.pos, ("a", "result"), src)
            self.assertEqual(fn.formals.by_refs, ("result",), src)


    async def test_bracket_byref_attribute(self):
        """
        [out byref: True] marks a by-ref formal via the explicit bracket form
        """

        expr, mod = self.compile_expr("(lambda [a [out byref: True]] out)")
        fn = await expr()
        self.assertEqual(fn.formals.pos, ("a", "out"))
        self.assertEqual(fn.formals.by_refs, ("out",))


    async def test_no_by_refs_means_empty(self):
        """
        a plain formals list reports no by-ref names
        """

        expr, mod = self.compile_expr("(lambda [a b] a)")
        fn = await expr()
        self.assertIsNone(fn.formals.by_refs)


    async def test_write_through(self):
        """
        set! on a by-ref formal writes through to the caller's binding
        """

        src = """
        (begin
          (define add-a [a &result] (set! result (+ a 1)))
          (define value 100)
          (add-a value &value)
          value)
        """
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), 101)


    async def test_terse_keyword_by_ref(self):
        """
        &acc: V is a keyword by-ref formal -- the & is dropped, name recorded
        """

        expr, mod = self.compile_expr("(lambda [a &acc: 5] acc)")
        fn = await expr()
        self.assertEqual(fn.formals.pos, ("a",))
        self.assertEqual(fn.formals.kwd, ("acc",))
        self.assertEqual(fn.formals.by_refs, ("acc",))


    async def test_terse_kwonly_by_ref(self):
        """
        a by-ref keyword-only formal after * is recognized and stripped
        """

        expr, mod = self.compile_expr("(lambda [a *: rest &acc: 5] acc)")
        fn = await expr()
        self.assertEqual(fn.formals.kwd_only, ("acc",))
        self.assertEqual(fn.formals.by_refs, ("acc",))


    async def test_terse_keyword_write_through(self):
        """
        a terse keyword by-ref writes through to an empty caller cell
        """

        src = """
        (begin
          (define fill [&out: 99] out)
          (define mine)
          (fill out: &mine)
          mine)
        """
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), 99)


    async def test_floor_when_omitted(self):
        """
        a by-ref formal with a floor, omitted, uses the floor privately (case C)
        """

        src = """
        (begin
          (define fill [[out byref: True value: 99]] out)
          (fill))
        """
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), 99)


    async def test_floor_fills_empty_caller_cell(self):
        """
        an empty caller cell with a floor is pre-filled through the ref (case B)
        """

        src = """
        (begin
          (define fill [[out byref: True value: 99]] out)
          (define mine)
          (fill &mine)
          mine)
        """
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), 99)


    async def test_positional_bare_value_raises(self):
        """
        a bare value passed to a by-ref positional is a TypeError
        """

        src = """
        (begin
          (define add-a [a &result] (set! result (+ a 1)))
          (add-a 1 2))
        """
        expr, mod = self.compile_expr(src)
        with self.assertRaises(TypeError):
            await expr()


    async def test_kwarg_bare_value_raises(self):
        """
        a bare value supplied to a by-ref keyword is a TypeError; a defaulted
        (omitted) by-ref keyword is fine
        """

        src = """
        (begin
          (define fill [&out: 99] out)
          (fill out: 7))
        """
        expr, mod = self.compile_expr(src)
        with self.assertRaises(TypeError):
            await expr()


# The end.
