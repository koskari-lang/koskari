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
Tests for the compile/pragma syntax forms from koskari.builtins: the set form
(pragma [ns/key value ...]), the read form (pragma ns/key), and the object
form (pragma).

Requires import koskari.builtins so bootstrap definitions are loaded.
"""


import pytest
import unittest

from koskari.compile.pragma import (BracketCalls, KeywordLiterals,
                                    ResolutionMode, StrayColon)
from koskari.lib import Module, ModuleNamespace
from koskari.module import build_module_load_facets
from koskari.pragma import ModulePragma
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class PragmaSyntaxHelpers(unittest.IsolatedAsyncioTestCase):

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


    def module_pragma(self, module):
        return vars(module)["__pragma__"]


class TestPragmaObjectForm(PragmaSyntaxHelpers):

    async def test_pragma_returns_module_pragma(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(module, compiler, "(pragma)")
        result = await expr()
        self.assertIsInstance(result, ModulePragma)
        self.assertIs(result, self.module_pragma(module))


class TestPragmaReadForm(PragmaSyntaxHelpers):

    async def test_read_default_value(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler, "(pragma compiler/call-brackets)")
        self.assertEqual(BracketCalls.WARN, await expr())


class TestPragmaSetForm(PragmaSyntaxHelpers):

    async def test_set_symbol_value(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler, "(pragma [compiler/call-brackets 'error])")
        await expr()
        self.assertEqual(
            BracketCalls.ERROR,
            self.module_pragma(module).get("compiler/call-brackets"))


    async def test_bare_key_uses_parameter_default(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler, "(pragma [compiler/call-brackets])")
        await expr()
        self.assertEqual(
            BracketCalls.WARN,
            self.module_pragma(module).get("compiler/call-brackets"))


    async def test_set_stray_colon(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler, "(pragma [compiler/stray-colon 'error])")
        await expr()
        self.assertEqual(
            StrayColon.ERROR,
            self.module_pragma(module).get("compiler/stray-colon"))
        # the declaration reaches the watching compiler, not just the pragma
        self.assertIs(StrayColon.ERROR, compiler.stray_colon)


    async def test_set_keyword_literals(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler, "(pragma [compiler/keyword-literals 'allow])")
        await expr()
        self.assertEqual(
            KeywordLiterals.ALLOW,
            self.module_pragma(module).get("compiler/keyword-literals"))
        self.assertIs(KeywordLiterals.ALLOW, compiler.keyword_literals)


    async def test_set_boolean_value(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler, "(pragma [compiler/tco-enable False])")
        await expr()
        self.assertEqual(
            False, self.module_pragma(module).get("compiler/tco-enable"))


    async def test_set_resolution_mode(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler, "(pragma [compiler/resolution 'scheme])")
        await expr()
        self.assertEqual(
            ResolutionMode.SCHEME,
            self.module_pragma(module).get("compiler/resolution"))


    async def test_multiple_settings_in_one_form(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler,
            "(pragma [compiler/tco-enable False] [reader/brackets False])")
        await expr()
        p = self.module_pragma(module)
        self.assertEqual(False, p.get("compiler/tco-enable"))
        self.assertEqual(False, p.get("reader/brackets"))


    async def test_set_rebinds_pragma_object(self):
        module, compiler = self.make_module()
        before = self.module_pragma(module)
        expr = self.compile_expr(
            module, compiler, "(pragma [compiler/tco-enable False])")
        await expr()
        self.assertIsNot(before, self.module_pragma(module))


    async def test_read_reflects_prior_set(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler,
            "(begin (pragma [compiler/call-brackets 'warn])"
            " (pragma compiler/call-brackets))")
        self.assertEqual(BracketCalls.WARN, await expr())


    async def test_unknown_setting_raises(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler, "(pragma [bogus/key True])")
        with self.assertRaises(ValueError):
            await expr()


class TestPragmaReaderEffects(PragmaSyntaxHelpers):
    """
    Pragma set forms propagate to the module's __reader__ facet
    """

    async def test_reader_fences_off_restores_quasiquote(self):
        from koskari.lib import symbol

        module, compiler = self.make_module()
        reader = vars(module)["__reader__"]

        result = reader.read(source_str("```json\n{}\n```\n", __file__))
        head, _tail = result
        self.assertIs(head, symbol("apply-fence"))

        expr = self.compile_expr(
            module, compiler, "(pragma [reader/fences False])")
        await expr()

        result = reader.read(source_str("```foo\n", __file__))
        head, _tail = result
        self.assertIs(head, symbol("quasiquote"))


# The end.
