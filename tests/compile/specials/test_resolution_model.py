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
Tests for the Koskari resolution model: explicit declarations for writes,
implicit global reads, define-global, and typo detection.
"""


from . import SpecialsTestCase

from koskari.compile.pragma import KoskariPragma, ResolutionMode
from koskari.exceptions import CompilerError
from koskari.lib import FunctionSpec

import pytest

pytestmark = pytest.mark.phase_specials


class TestResolutionModelModuleLevel(SpecialsTestCase):

    async def test_module_level_setq_works(self):
        """setq at top level (no enclosing function) continues to work"""
        src_str = """
        (begin
          (setq x 1)
          x)
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(1, result)


class TestResolutionModelTypoError(SpecialsTestCase):

    async def test_typo_in_nested_scope_raises(self):
        """setq to undeclared name inside function raises CompilerError"""
        src_str = """
        ((lambda ()
           (setq mistyped 1)))
        """
        with self.assertRaises(CompilerError) as ctx:
            self.compile_expr(src_str)
        self.assertIn('mistyped', str(ctx.exception))
        self.assertIn('not declared', str(ctx.exception))


class TestResolutionModelDefineGlobal(SpecialsTestCase):

    async def test_define_global_then_setq_works(self):
        """define-global then setq to same name works"""
        src_str = """
        (begin
          (define-global x 1)
          (setq x 2)
          x)
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(2, result)


class TestResolutionModelImplicitRead(SpecialsTestCase):

    async def test_implicit_read_unchanged(self):
        """Implicit read (VAR_LOAD) of global still works"""
        def printer(x):
            return x

        src_str = """
        (call printer undefined_global)
        """
        expr, mod = self.compile_expr(src_str, printer=printer,
                                      undefined_global=99)
        result = await expr()
        self.assertEqual(99, result)


class TestResolutionModelNestedWrite(SpecialsTestCase):

    async def test_nested_write_to_closure_works(self):
        """Outer define x, inner setq x works (closure write)"""
        src_str = """
        (let [[x 0]]
          (begin
            ((lambda ()
               (setq x 1)))
            x))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(1, result)


    async def test_nested_define_setter_closure_write(self):
        """define x, inner define setter with setq x works (closure write)"""
        src_str = """
        ((lambda ()
           (define x 101)
           (define getter (lambda () x))
           (define setter (lambda [v] (setq x v)))
           (call (lambda [g s]
             (call s 42)
             (call g))
             getter setter)))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(42, result)


    async def test_nested_let_with_closure_read(self):
        """Nested let with lambda closing over both bindings; read-only closure"""
        src_str = """
        (let [[a 1]]
          (let [[b 2]]
            ((lambda () (call + a b)))))
        """
        expr, mod = self.compile_expr(src_str, **{'+': lambda x, y: x + y})
        result = await expr()
        self.assertEqual(3, result)


class TestResolutionModelDefineGlobalNested(SpecialsTestCase):

    async def test_define_global_nested_shared_counter(self):
        """define-global counter; inner lambda setq counter; shared across calls"""
        src_str = """
        ((lambda ()
           (define-global counter 0)
           (define inc (lambda ()
             (setq counter (call + counter 1))
             counter))
           (call inc)
           (call inc)
           counter))
        """
        expr, mod = self.compile_expr(src_str, **{'+': lambda x, y: x + y})
        result = await expr()
        self.assertEqual(2, result)


    async def test_define_global_shadows_local(self):
        """Outer define x; inner define-global x; inner setq does not touch outer"""
        src_str = """
        ((lambda ()
           (define x 0)
           (define inner (lambda ()
             (define-global x 99)
             (setq x 1)))
           (call inner)
           x))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(0, result)
        self.assertEqual(vars(mod).deref('x'), 1)


def _find_lambda_codes(code):
    """
    Yield (code, nested_code) for each FunctionSpec in code's constants
    """

    for const in (code.constants_pool or ()):
        if isinstance(const, FunctionSpec) and const.code is not None:
            yield const.code
            for nested in _find_lambda_codes(const.code):
                yield nested


class TestResolutionModelNamespaceSpec(SpecialsTestCase):

    async def test_nested_let_closure_lambda_has_closure_syms(self):
        """
        Lambda in nested let closing over a,b has closure_syms=('a','b')
        """

        pragma = KoskariPragma()
        src_str = """
        (let [[a 1]]
          (let [[b 2]]
            (lambda () (call + a b))))
        """
        code = self.compile_to_code(
            src_str, resolution_mode=ResolutionMode.KOSKARI,
            resolution_instance=pragma,
            **{'+': lambda x, y: x + y})

        lambdas = list(_find_lambda_codes(code))
        self.assertGreaterEqual(len(lambdas), 1, 'expected at least one lambda')

        inner = lambdas[0]
        closure = set(inner.namespace_spec.closure_syms)

        self.assertIn('a', closure, 'lambda should close over a')
        self.assertIn('b', closure, 'lambda should close over b')
        self.assertEqual(
            pragma.get_request_for_write_syms(inner),
            (),
            'lambda is read-only over a,b')


    async def test_closure_setter_has_request_for_write_syms(self):
        """
        Setter lambda closing over x has request_for_write_syms=('x',)
        """

        pragma = KoskariPragma()
        src_str = """
        ((lambda ()
           (define x 101)
           (define setter (lambda [v] (setq x v)))
           (call setter 42)))
        """
        code = self.compile_to_code(
            src_str, resolution_mode=ResolutionMode.KOSKARI,
            resolution_instance=pragma)

        lambdas = list(_find_lambda_codes(code))
        self.assertGreaterEqual(len(lambdas), 2, 'outer and setter lambda')

        setter = next((c for c in lambdas
                       if 'v' in c.namespace_spec.local_syms), None)
        self.assertIsNotNone(setter, 'expected setter lambda with param v')

        self.assertEqual(
            set(setter.namespace_spec.closure_syms),
            {'x'},
            'setter should close over x')
        self.assertEqual(
            set(pragma.get_request_for_write_syms(setter)),
            {'x'},
            'setter writes to x')


    async def test_define_global_module_level_has_global_syms(self):
        """define-global at module level yields global_syms in root Code"""
        src_str = """
        (begin
          (define-global x 1)
          (setq x 2)
          x)
        """
        code = self.compile_to_code(src_str)
        nspec = code.namespace_spec
        self.assertIn('x', nspec.global_syms,
                      'x should be in global_syms')


# The end.
