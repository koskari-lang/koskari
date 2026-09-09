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
Tests for koskari.compile define special form.
"""


import pytest

from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.compile import Compiler
from koskari.lib import Module, symbol
from koskari.reader import LayoutReader, source_str

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


def _register_specials(ns, specials):
    for sym, fn in specials.items():
        ns.store(str(sym), fn)


class TestDefineDeclareOnly(SpecialsTestCase):

    async def test_define_then_setq_then_read(self):
        src_str = """
        (begin
          (define x)
          (setq x 42)
          x)
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(42, result)


class TestDefineWithExpr(SpecialsTestCase):

    async def test_define_then_read(self):
        src_str = """
        (begin
          (define y 99)
          y)
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(99, result)


class TestDefineFunctionShorthand(SpecialsTestCase):

    async def test_define_double(self):
        src_str = """
        (begin
          (define double [x] (mul x 2))
          (double 5))
        """
        expr, mod = self.compile_expr(src_str, mul=lambda a, b: a * b)
        result = await expr()
        self.assertEqual(10, result)


    async def test_define_fact(self):
        src_str = """
        (begin
          (define fact [n]
            (cond
              [(eq n 0) 1]
              [else: (mul n (fact (sub n 1)))]))
          (fact 5))
        """
        expr, mod = self.compile_expr(
            src_str,
            eq=lambda a, b: a == b,
            mul=lambda a, b: a * b,
            sub=lambda a, b: a - b)
        result = await expr()
        self.assertEqual(120, result)


    async def test_define_function_shorthand_inline_return_annotation(self):
        src_str = """
        (begin
          (define f :: int [x] (mul x 2))
          f)
        """
        expr, mod = self.compile_expr(src_str, mul=lambda a, b: a * b,
                                      int=int)
        f = await expr()
        self.assertIs(f.__annotations__['return'], int)
        self.assertEqual(await f(5), 10)


    async def test_define_function_shorthand_decl_bracket_still_works(self):
        src_str = """
        (begin
          (define [f :: int] [x] (mul x 2))
          f)
        """
        expr, mod = self.compile_expr(src_str, mul=lambda a, b: a * b,
                                      int=int)
        f = await expr()
        self.assertIs(f.__annotations__['return'], int)
        self.assertEqual(await f(5), 10)


    async def test_define_function_shorthand_type_colon_return_annotation(self):
        src_str = """
        (begin
          (define g type: int [x] (mul x 2))
          g)
        """
        expr, mod = self.compile_expr(src_str, mul=lambda a, b: a * b,
                                      int=int)
        g = await expr()
        self.assertIs(g.__annotations__['return'], int)
        self.assertEqual(await g(5), 10)


class TestDefineInlineTypedValue(SpecialsTestCase):

    async def test_define_inline_typed_value(self):
        src_str = """
        (begin
          (define z :: int 100)
          z)
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(100, result)


class TestDefineAnnotationBindings(SpecialsTestCase):

    def compile_fragment(self, src_str, lineno=1, **defns):
        mod_ns = self.get_glbls(**defns)
        module = Module(mod_ns)
        ns = vars(module)
        _register_specials(ns, _BUILTIN_REGISTRY)
        stream = source_str(src_str, __file__, line_no=lineno)
        source = LayoutReader(ns).read(stream)
        compiler = Compiler("<unittest>", module, filename=__file__)
        return compiler.full_compile(source)

    def test_value_branch_records_annotation_binding(self):
        f = self.compile_fragment("(define z :: int 100)")
        self.assertEqual(len(f.annotation_bindings), 1)
        self.assertEqual(f.annotation_bindings[0][0], "z")
        self.assertEqual(f.annotation_bindings[0][1], symbol("int"))

    def test_function_shorthand_no_fragment_annotation_binding(self):
        f = self.compile_fragment(
            "(define f :: int [x] (mul x 2))", mul=lambda a, b: a * b)
        self.assertEqual(f.annotation_bindings, [])


# The end.
