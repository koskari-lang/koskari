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
from koskari.exceptions import SpecialError, SyntaxMatchError
from koskari.lib import Module, symbol
from koskari.reader import IndentAwareStream, LayoutReader, source_str

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


class TestDefineFunction(SpecialsTestCase):

    async def test_define_double(self):
        src_str = """
        (begin
          (define [double x] (mul x 2))
          (double 5))
        """
        expr, mod = self.compile_expr(src_str, mul=lambda a, b: a * b)
        result = await expr()
        self.assertEqual(10, result)


    async def test_define_fact(self):
        src_str = """
        (begin
          (define [fact n]
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


    async def test_define_function_return_annotation_through_function(self):
        src_str = """
        (begin
          (define f (function f :: int [x] (mul x 2)))
          f)
        """
        expr, mod = self.compile_expr(src_str, mul=lambda a, b: a * b,
                                      int=int)
        f = await expr()
        self.assertIs(f.__annotations__['return'], int)
        self.assertEqual(await f(5), 10)


    async def test_define_function_type_colon_return_through_function(self):
        src_str = """
        (begin
          (define g (function g type: int [x] (mul x 2)))
          g)
        """
        expr, mod = self.compile_expr(src_str, mul=lambda a, b: a * b,
                                      int=int)
        g = await expr()
        self.assertIs(g.__annotations__['return'], int)
        self.assertEqual(await g(5), 10)


class TestDefineSchemeShape(SpecialsTestCase):

    async def test_paren_head(self):
        src_str = """
        (begin
          (define (double x) (mul x 2))
          (double 4))
        """
        expr, mod = self.compile_expr(src_str, mul=lambda a, b: a * b)
        self.assertEqual(8, await expr())


    async def test_head_without_formals(self):
        expr, mod = self.compile_expr("(begin (define [answer] 42) (answer))")
        self.assertEqual(42, await expr())


    async def test_function_is_named(self):
        expr, mod = self.compile_expr("(begin (define [named] 1) named)")
        self.assertEqual("named", (await expr()).__name__)


    async def test_old_shorthand_does_not_match(self):
        with self.assertRaises(SyntaxMatchError):
            self.compile_expr("(define f [x] x)")


    async def test_value_that_reads_as_formals(self):
        # a name and a value, even when the value is a call
        src_str = """
        (begin
          (define v [mk])
          v)
        """
        expr, mod = self.compile_expr(src_str, mk=lambda: 7)
        self.assertEqual(7, await expr())


    async def test_pair_head_is_a_function_not_an_unpacking(self):
        src_str = """
        (begin
          (define [a b] (mul b 3))
          (a 2))
        """
        expr, mod = self.compile_expr(src_str, mul=lambda x, y: x * y)
        self.assertEqual(6, await expr())


    async def test_head_must_be_named(self):
        with self.assertRaises(SpecialError):
            self.compile_expr("(define [1 x] x)")


    async def test_define_global_function_from_body(self):
        src_str = """
        (begin
          (define [setup]
            (define-global [helper y] (mul y 10))
            None)
          (setup)
          (helper 2))
        """
        expr, mod = self.compile_expr(src_str, mul=lambda a, b: a * b)
        self.assertEqual(20, await expr())
        self.assertIn("helper", vars(mod))


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


    async def test_define_inline_typed_value_is_a_call(self):
        # a value that would read as a formals list, (dict) or (max 1 2),
        # is the stored expression: a typed function needs a body
        src_str = """
        (begin
          (define b :: dict (dict))
          (define a :: int (maxi 1 2))
          #(a b))
        """
        expr, mod = self.compile_expr(src_str, dict=dict, maxi=max)
        self.assertEqual((2, {}), await expr())


class TestDefineAnnotationBindings(SpecialsTestCase):

    def compile_fragment(self, src_str, lineno=1, **defns):
        mod_ns = self.get_glbls(**defns)
        module = Module(mod_ns)
        ns = vars(module)
        _register_specials(ns, _BUILTIN_REGISTRY)
        stream = IndentAwareStream.from_source(
            source_str(src_str, __file__, line_no=lineno))
        source = LayoutReader(ns).read(stream)
        compiler = Compiler("<unittest>", module, filename=__file__)
        return compiler.full_compile(source)

    def test_value_branch_records_annotation_binding(self):
        f = self.compile_fragment("(define z :: int 100)")
        self.assertEqual(len(f.annotation_bindings), 1)
        self.assertEqual(f.annotation_bindings[0][0], "z")
        self.assertEqual(f.annotation_bindings[0][1], symbol("int"))

    def test_function_form_no_fragment_annotation_binding(self):
        f = self.compile_fragment(
            "(define [f x] (mul x 2))", mul=lambda a, b: a * b)
        self.assertEqual(f.annotation_bindings, [])


class TestDefineKeywordHeadedFormals(SpecialsTestCase):

    async def test_define_return_type_with_keyword_headed_formal(self):
        src = """
        (begin
          (define foo
            (function foo :: int [[x :: int] [y: 1 :: int]] (add x y)))
          foo)
        """
        expr, mod = self.compile_expr(src, int=int, add=lambda a, b: a + b)
        fn = await expr()
        self.assertEqual(
            fn.__annotations__, {"x": int, "y": int, "return": int})
        self.assertEqual(await fn(1), 2)
        self.assertEqual(await fn(1, 5), 6)
        self.assertEqual(await fn(1, y=7), 8)



class TestDefineBareDoubleColonFormals(SpecialsTestCase):

    async def test_define_lambda_bare_double_colon_rejected(self):
        src = "(define f (lambda [n :: int] n))"
        with self.assertRaises(SpecialError) as cm:
            self.compile_expr(src, int=int)
        self.assertIn("[(n :: int)]", str(cm.exception))


    async def test_define_inline_bare_double_colon_rejected(self):
        src = "(define [f n :: int] n)"
        with self.assertRaises(SpecialError) as cm:
            self.compile_expr(src, int=int)
        self.assertIn("[(n :: int)]", str(cm.exception))


    async def test_define_parenthesized_annotation_still_binds(self):
        src = """
        (begin
          (define [f (n :: int)] n)
          f)
        """
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        self.assertEqual(fn.__annotations__, {"n": int})
        self.assertEqual(await fn(3), 3)


# The end.
