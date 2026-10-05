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
Tests for the resolution pragma system: KoskariPragma, PythonPragma, SchemePragma.
"""


import pytest

from koskari.compile.pragma import ResolutionMode
from koskari.exceptions import CompilerError

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestKoskariPragma(SpecialsTestCase):

    async def test_module_level_setq_with_explicit_pragma(self):
        """KoskariPragma: setq at top level works (same as default)"""
        src_str = """
        (begin
          (setq x 1)
          x)
        """
        expr, mod = self.compile_expr(
            src_str, resolution_mode=ResolutionMode.KOSKARI)
        result = await expr()
        self.assertEqual(1, result)


    async def test_typo_in_nested_scope_raises_with_explicit_pragma(self):
        """KoskariPragma: setq to undeclared name inside function raises"""
        src_str = """
        ((lambda ()
           (setq mistyped 1)))
        """
        with self.assertRaises(CompilerError) as ctx:
            self.compile_expr(src_str, resolution_mode=ResolutionMode.KOSKARI)
        self.assertIn('mistyped', str(ctx.exception))
        self.assertIn('not declared', str(ctx.exception))


    async def test_undeclared_write_error_points_at_the_write(self):
        """KoskariPragma: the error carries the written name's position"""
        src_str = """
        ((lambda ()
           (setq mistyped 1)))
        """
        with self.assertRaises(CompilerError) as ctx:
            self.compile_expr(src_str, resolution_mode=ResolutionMode.KOSKARI)
        self.assertEqual(ctx.exception.position, (3, 9))


    async def test_undeclared_write_in_let_body_points_at_the_write(self):
        """KoskariPragma: a write inside an inline let scope is located too"""
        src_str = """
        ((lambda ()
           (let [[a 1]]
             (setq mistyped 2))))
        """
        with self.assertRaises(CompilerError) as ctx:
            self.compile_expr(src_str, resolution_mode=ResolutionMode.KOSKARI)
        self.assertEqual(ctx.exception.position, (4, 11))


class TestPythonPragma(SpecialsTestCase):

    async def test_implicit_local_on_first_write(self):
        """PythonPragma: setq without prior declare creates implicit local"""
        src_str = """
        ((lambda ()
           (setq x 1)
           x))
        """
        expr, mod = self.compile_expr(
            src_str, resolution_mode=ResolutionMode.PYTHON)
        result = await expr()
        self.assertEqual(1, result)


    async def test_implicit_local_does_not_leak_to_module(self):
        """PythonPragma: implicit local in lambda does not create module binding"""
        src_str = """
        ((lambda ()
           (setq local_var 42)
           local_var))
        """
        expr, mod = self.compile_expr(
            src_str, resolution_mode=ResolutionMode.PYTHON)
        result = await expr()
        self.assertEqual(42, result)
        with self.assertRaises((KeyError, NameError)):
            vars(mod).deref('local_var')


    async def test_setq_inside_let_writes_the_function_binding(self):
        """
        A name the function declares is written through a let, not
        shadowed by a fresh let-local
        """

        src_str = """
        ((lambda ()
           (define x 0)
           (let [[y 1]] (setq x y))
           x))
        """
        expr, mod = self.compile_expr(
            src_str, resolution_mode=ResolutionMode.PYTHON)
        self.assertEqual(1, await expr())


    async def test_setq_inside_let_writes_a_parameter(self):
        src_str = """
        ((lambda (x)
           (let [[y 1]] (setq x y))
           x) 5)
        """
        expr, mod = self.compile_expr(
            src_str, resolution_mode=ResolutionMode.PYTHON)
        self.assertEqual(1, await expr())


    async def test_setq_to_the_let_binding_writes_the_let(self):
        src_str = """
        ((lambda ()
           (define y 0)
           (let [[y 1]] (setq y 2) y)))
        """
        expr, mod = self.compile_expr(
            src_str, resolution_mode=ResolutionMode.PYTHON)
        self.assertEqual(2, await expr())


    async def test_undeclared_write_inside_let_declares_in_the_function(self):
        """
        A first write is a declaration in the nearest function, so the
        name is visible after the let
        """

        src_str = """
        ((lambda ()
           (let [[y 1]] (setq x y))
           x))
        """
        expr, mod = self.compile_expr(
            src_str, resolution_mode=ResolutionMode.PYTHON)
        self.assertEqual(1, await expr())


    async def test_write_inside_nested_lambda_stays_its_own_local(self):
        src_str = """
        ((lambda ()
           (define x 0)
           ((lambda () (setq x 1)))
           x))
        """
        expr, mod = self.compile_expr(
            src_str, resolution_mode=ResolutionMode.PYTHON)
        self.assertEqual(0, await expr())


class TestSchemePragma(SpecialsTestCase):

    async def test_unsatisfied_write_promotes_to_global(self):
        """SchemePragma: setq to undeclared name in lambda compiles and runs"""
        src_str = """
        ((lambda ()
           (setq x 1)
           x))
        """
        expr, mod = self.compile_expr(
            src_str, resolution_mode=ResolutionMode.SCHEME)
        result = await expr()
        self.assertEqual(1, result)


    async def test_implicit_global_write_leaks_to_module(self):
        """SchemePragma: unsatisfied write in lambda creates module binding"""
        src_str = """
        ((lambda ()
           (setq scheme_global 42)
           scheme_global))
        """
        expr, mod = self.compile_expr(
            src_str, resolution_mode=ResolutionMode.SCHEME)
        result = await expr()
        self.assertEqual(42, result)
        self.assertEqual(42, vars(mod).deref('scheme_global'))


# The end.
