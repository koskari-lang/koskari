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
Tests for the assert macro from koskari.builtins: (assert expr) and
(assert expr message), and the compiler/enable-asserts pragma that gates
its expansion at compile time.

Requires import koskari.builtins so bootstrap definitions are loaded.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import pytest
import unittest

from koskari.lib import Module, ModuleNamespace, symbol
from koskari.module import build_module_load_facets
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class AssertSyntaxHelpers(unittest.IsolatedAsyncioTestCase):

    def make_module(self):
        """
        A fresh module with the pragma load facets (__pragma__, __compiler__,
        __reader__) attached, returning the module and its compiler
        """

        import koskari.builtins as builtins

        mod_ns = ModuleNamespace("test", dict(vars(builtins)))
        module = Module(mod_ns)
        compiler, _reader = build_module_load_facets(module)
        return module, compiler


    def compile_expr(self, module, compiler, src_str):
        source = default_reader.read(source_str(src_str, __file__))
        fragment = compiler.full_compile(source)
        return fragment.as_expression(vars(module))


    async def eval_src(self, module, compiler, src_str):
        return await self.compile_expr(module, compiler, src_str)()


    async def set_asserts(self, module, compiler, enabled):
        """
        Declare the compiler/enable-asserts pragma. The gate is shared
        state in the builtins closure, so tests that disable it must
        restore it.
        """

        setting = "True" if enabled else "False"
        await self.eval_src(
            module, compiler,
            f"(pragma [compiler/enable-asserts {setting}])")


class TestAssertEnabled(AssertSyntaxHelpers):

    async def test_passing_assert(self):
        module, compiler = self.make_module()
        result = await self.eval_src(
            module, compiler, "(begin (assert True) 'ok)")
        self.assertIs(symbol("ok"), result)


    async def test_failing_assert_raises_bare(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(module, compiler, "(assert False)")
        with self.assertRaises(AssertionError) as caught:
            await expr()
        self.assertEqual((), caught.exception.args)


    async def test_failing_assert_raises_with_message(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler, '(assert (== 1 2) "one is not two")')
        with self.assertRaises(AssertionError) as caught:
            await expr()
        self.assertEqual(("one is not two",), caught.exception.args)


    async def test_falsey_condition_raises(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(module, compiler, "(assert 0)")
        with self.assertRaises(AssertionError):
            await expr()


    async def test_message_not_evaluated_on_pass(self):
        module, compiler = self.make_module()
        result = await self.eval_src(
            module, compiler,
            "(begin (assert True (missing-fn)) 'ok)")
        self.assertIs(symbol("ok"), result)


class TestAssertDisabled(AssertSyntaxHelpers):

    async def test_disabled_compiles_away(self):
        module, compiler = self.make_module()
        await self.set_asserts(module, compiler, False)
        try:
            result = await self.eval_src(
                module, compiler, "(begin (assert False) 'ok)")
            self.assertIs(symbol("ok"), result)
        finally:
            await self.set_asserts(module, compiler, True)


    async def test_disabled_skips_condition(self):
        module, compiler = self.make_module()
        await self.set_asserts(module, compiler, False)
        try:
            result = await self.eval_src(
                module, compiler, "(begin (assert (missing-fn)) 'ok)")
            self.assertIs(symbol("ok"), result)
        finally:
            await self.set_asserts(module, compiler, True)


    async def test_gating_is_compile_time(self):
        # an assert compiled while disabled stays gone, even when the
        # pragma is re-enabled before it is evaluated
        module, compiler = self.make_module()
        await self.set_asserts(module, compiler, False)
        try:
            expr = self.compile_expr(
                module, compiler, "(begin (assert False) 'ok)")
        finally:
            await self.set_asserts(module, compiler, True)

        self.assertIs(symbol("ok"), await expr())

        expr = self.compile_expr(
            module, compiler, "(begin (assert False) 'ok)")
        with self.assertRaises(AssertionError):
            await expr()


class TestAssertPragma(AssertSyntaxHelpers):

    async def test_baseline_enabled(self):
        module, compiler = self.make_module()
        result = await self.eval_src(
            module, compiler, "(pragma compiler/enable-asserts)")
        self.assertIs(True, result)


    async def test_disable_reflected_in_pragma(self):
        module, compiler = self.make_module()
        await self.set_asserts(module, compiler, False)
        try:
            result = await self.eval_src(
                module, compiler, "(pragma compiler/enable-asserts)")
            self.assertIs(False, result)
        finally:
            await self.set_asserts(module, compiler, True)


    async def test_bare_declaration_enables(self):
        module, compiler = self.make_module()
        await self.set_asserts(module, compiler, False)
        try:
            await self.eval_src(
                module, compiler, "(pragma [compiler/enable-asserts])")
            result = await self.eval_src(
                module, compiler, "(pragma compiler/enable-asserts)")
            self.assertIs(True, result)

            expr = self.compile_expr(module, compiler, "(assert False)")
            with self.assertRaises(AssertionError):
                await expr()
        finally:
            await self.set_asserts(module, compiler, True)


# The end.
