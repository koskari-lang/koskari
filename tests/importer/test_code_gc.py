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
Code objects take part in cycle collection for their cache slots

A method-call site caches the receiver type and the resolved function
in a slot trailing the Code object. A class whose methods call through
such a site forms type, function, code, slot, type; the collector must
be able to see and break it. Requires import koskari.builtins for the
define-class macro.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import gc
import unittest
import weakref

import pytest

from koskari.compile import Compiler
from koskari.lib import Code, Module, ModuleNamespace
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class TestCodeCycleCollection(unittest.IsolatedAsyncioTestCase):

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


    def test_code_is_gc_tracked(self):
        """
        every Code object is visible to the collector
        """

        expr, _mod = self.compile_expr("(+ 1 2)")
        self.assertIsInstance(expr.code, Code)
        self.assertTrue(gc.is_tracked(expr.code))


    async def test_class_calling_own_method_is_collected(self):
        """
        a class whose method calls another of its methods through a
        cached call site is collected once nothing else holds it
        """

        src = """
        (begin
          (define-class Leaky []
            (define helper [self] 1)
            (define run [self] (self.helper)))
          (define inst (Leaky))
          (inst.run)
          (inst.run)
          (inst.run)
          Leaky)
        """
        expr, mod = self.compile_expr(src)
        cls = await expr()
        self.assertEqual(cls.__name__, "Leaky")
        alive = weakref.ref(cls)

        del cls, expr, mod
        gc.collect()
        self.assertIsNone(alive())


    async def test_instance_holding_own_bound_method_is_collected(self):
        """
        an instance that stores one of its own bound methods forms a
        cycle through the bound method and is collected
        """

        src = """
        (begin
          (define-class Holder []
            (define m [self] 1))
          (define o (Holder))
          (set-attr o cb o.m)
          o)
        """
        expr, mod = self.compile_expr(src)
        inst = await expr()
        self.assertEqual(type(inst.cb).__name__, "BoundMethod")
        self.assertTrue(gc.is_tracked(inst.cb))
        alive = weakref.ref(inst)

        del inst, expr, mod
        gc.collect()
        self.assertIsNone(alive())


# The end.
