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
tests.importer.test_refs

End-to-end ref surface from the guide chapter
docs/guide/references-and-storage-indirection.md and its companion
examples/refs.ki: getting a ref (& sugar, &(item ...) compound reader),
reading with deref, value-level write-through with the store method or
(set! (deref ref) value), the set!-rebinds-the-name trap, and bind! making
the indirection invisible.
Runs at phase_basics because the forms use the & / ref / deref / bind! /
set! macros from bootstrap + basics.ki.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile import Compiler
from koskari.lib import Module, ModuleNamespace
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class TestRefs(unittest.IsolatedAsyncioTestCase):


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


    async def test_amp_ref_then_deref(self):
        """
        &x gets a ref to the local; deref reads the value through it
        """

        expr, mod = self.compile_expr("(let [[x 7]] (deref &x))")
        self.assertEqual(await expr(), 7)


    async def test_amp_call_compound_reader_deref(self):
        """
        &(item xs 0) is a ref to a list slot; deref reads that element
        """

        src = "(begin (define xs #[1 2 3 4]) (deref &(item xs 0)))"
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), 1)


    async def test_store_writes_through_to_original(self):
        """
        the ref store method writes through to the named binding
        """

        src = "(begin (define v 10) (define r &v) (r.store 20) v)"
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), 20)


    async def test_set_on_ref_holder_rebinds_not_writes_through(self):
        """
        the trap: set! on the name holding a ref rebinds it; the original is
        untouched
        """

        src = "(begin (define v 10) (define r &v) (set! r 99) v)"
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), 10)


    async def test_set_bang_deref_writes_through_to_original(self):
        """
        set! on a deref place writes through; unlike set! on the ref holder
        """

        src = "(begin (define v 10) (define r &v) (set! (deref r) 20) v)"
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), 20)


    async def test_set_bang_compile_deref_writes_through(self):
        """
        compile/deref in the set! place expands like bare deref
        """

        src = "(begin (define v 10) (define r &v) (set! (compile/deref r) 30) v)"
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), 30)


    async def test_set_bang_deref_amp_ref_in_let(self):
        """
        set! (deref &x) writes through a ref taken directly from a local
        """

        src = "(let [[x 5]] (set! (deref &x) 99) x)"
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), 99)


    async def test_set_bang_deref_item_ref_writes_back_into_list(self):
        """
        set! (deref cell) on an item ref mutates the slot, not the name cell
        """

        src = ("(begin (define xs #[1 2 3]) (define cell &(item xs 1))"
               " (set! (deref cell) 99) (item xs 1))")
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), 99)


    async def test_bind_makes_indirection_invisible(self):
        """
        bind! installs a ref as a name's storage; plain set! writes through
        """

        src = "(begin (define total 5) (bind! a &total) (set! a 42) total)"
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), 42)


    async def test_bind_to_item_ref_writes_back_into_list(self):
        """
        bind! a name to an item ref, then ordinary set! mutates the list slot
        """

        src = ("(begin (define xs #[1 2 3 4]) (bind! first &(item xs 0))"
               " (set! first 1000) (item xs 0))")
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), 1000)


    async def test_byref_call_with_item_ref_writes_back(self):
        """
        a by-ref formal accepts &(item ...); the callee writes back into xs
        """

        src = ("(begin (define xs #[1 2 3 4])"
               " (define bump [&n] (set! n (+ n 1)))"
               " (bump &(item xs 2)) (item xs 2))")
        expr, mod = self.compile_expr(src)
        self.assertEqual(await expr(), 4)


    async def test_by_ref_formal_rejects_plain_value(self):
        """
        the guardrail: a by-ref formal refuses a plain value, naming the formal
        """

        src = ("(begin (define bump [&n] (set! n (+ n 1)))"
               " (bump 5))")
        expr, mod = self.compile_expr(src)
        with self.assertRaisesRegex(
                TypeError, r"bump\(\) by-ref formal n requires a Ref"):
            await expr()


# The end.
