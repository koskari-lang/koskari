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
Tests for the evaluate, compile, and get-compiler runtime functions from
basics.ki

evaluate must accept a source form, a Code object, or an already
compiled Expression, and return the value of running it in the given
namespace. get-compiler must hand back the module's bound compiler, or
rebuild and bind one when the load-time facets are gone. Requires
import koskari.builtins so the functions and their macros are present.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import sys
import types
import unittest
from textwrap import dedent

import pytest

from koskari.compile import Compiler
from koskari.lib import Code, Expression, Module, ModuleNamespace, demand_call
from koskari.module import (
    find_module_reader, load_module_source_async, new_module)
from koskari.pragma import ModulePragma
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class TestEvaluate(unittest.IsolatedAsyncioTestCase):

    def compile_expr(self, src_str, lineno=1, **defns):
        import koskari.builtins as builtins
        bltns = vars(builtins)
        if defns:
            bltns = dict(bltns)
            bltns.update(defns)

        mod_ns = ModuleNamespace("test", bltns)
        module = Module(mod_ns)
        source = default_reader.read(
            source_str(dedent(src_str), __file__, line_no=lineno))
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.compile_expression(source)
        return fragment.as_expression(mod_ns), module


    async def test_evaluate_macro_one_argument(self):
        """
        (evaluate form) compiles and runs the form in (locals)
        """

        expr, _mod = self.compile_expr("(evaluate (+ 1 2))")
        self.assertEqual(await expr(), 3)


    async def test_evaluate_macro_explicit_namespace(self):
        """
        (evaluate form ns) runs the form in the given namespace
        """

        src_str = """
        (evaluate (+ x 2) (locals))
        """
        expr, _mod = self.compile_expr(src_str, x=40)
        self.assertEqual(await expr(), 42)


    async def test_evaluate_call_with_quoted_source(self):
        """
        the evaluate function takes a source object and a namespace
        """

        src_str = """
        (call evaluate '(* 6 7) (locals))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), 42)


    async def test_evaluate_call_with_code(self):
        """
        a Code object is turned into an expression in the namespace
        """

        src_str = """
        (call evaluate (attr (compileq (* 6 7)) code) (locals))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), 42)


    async def test_evaluate_call_with_expression(self):
        """
        an Expression is invoked as-is
        """

        src_str = """
        (call evaluate (compileq (* 6 7)) (locals))
        """
        expr, _mod = self.compile_expr(src_str)
        self.assertEqual(await expr(), 42)


    async def test_compileq_returns_expression(self):
        """
        compile returns an Expression whose code is a Code object
        """

        src_str = """
        (compileq (+ 1 2))
        """
        expr, _mod = self.compile_expr(src_str)
        compiled = await expr()
        self.assertIsInstance(compiled, Expression)
        self.assertIsInstance(compiled.code, Code)


class TestGetCompiler(unittest.IsolatedAsyncioTestCase):

    def compile_expr(self, src_str, lineno=1, name="test", **defns):
        import koskari.builtins as builtins
        bltns = vars(builtins)
        if defns:
            bltns = dict(bltns)
            bltns.update(defns)

        mod_ns = ModuleNamespace(name, bltns)
        module = Module(mod_ns)
        source = default_reader.read(
            source_str(dedent(src_str), __file__, line_no=lineno))
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.compile_expression(source)
        return fragment.as_expression(mod_ns), module, compiler


    async def test_returns_bound_compiler(self):
        """
        (get-compiler) hands back the module's __compiler__ when bound
        """

        expr, module, compiler = self.compile_expr("(get-compiler)")
        vars(module)['__compiler__'] = compiler
        self.assertIs(await expr(), compiler)


    async def test_rebuilds_and_binds_when_missing(self):
        """
        without __compiler__ a fresh compiler is built, bound, and reused
        """

        expr, module, compiler = self.compile_expr("(get-compiler)")
        ns = vars(module)
        self.assertNotIn('__compiler__', ns)

        rebuilt = await expr()
        self.assertIsInstance(rebuilt, Compiler)
        self.assertIsNot(rebuilt, compiler)
        self.assertIs(ns['__compiler__'], rebuilt)
        self.assertIsInstance(ns['__pragma__'], ModulePragma)

        self.assertIs(await expr(), rebuilt)


    async def test_rebuilds_for_a_plain_mapping(self):
        """
        get-compiler takes a plain mapping as the namespace, as
        find_module_pragma does, and names the compiler <module>
        """

        import koskari.builtins as builtins
        ns = dict(vars(builtins))
        compiler = demand_call(vars(builtins)['get-compiler'], ns)
        self.assertIsInstance(compiler, Compiler)
        self.assertEqual(compiler.name, "<module>")
        self.assertIs(ns['__compiler__'], compiler)
        self.assertIsInstance(ns['__pragma__'], ModulePragma)
        self.assertIs(demand_call(vars(builtins)['get-compiler'], ns),
                      compiler)


    async def test_walks_up_from_function_namespace(self):
        """
        (get-compiler) inside a function binds on the module, not locally
        """

        src_str = """
        ((lambda [] (get-compiler)))
        """
        expr, module, _compiler = self.compile_expr(src_str)
        ns = vars(module)
        rebuilt = await expr()
        self.assertIs(ns['__compiler__'], rebuilt)


    async def test_rebuild_inherits_the_package_pragma_and_module_name(self):
        """
        without __pragma__ the rebuild seeds from the nearest package's
        __package_pragma__ and names the compiler after the module
        """

        pkg = types.ModuleType("gcpkg")
        pkg.__package_pragma__ = ModulePragma(
            {"compiler/stray-colon": "error"})
        sys.modules["gcpkg"] = pkg
        self.addCleanup(sys.modules.pop, "gcpkg", None)

        expr, module, _compiler = self.compile_expr(
            "(get-compiler)", name="gcpkg.child")
        ns = vars(module)

        # expanding the macro bound the module's reader and pragma;
        # drop them so the call rebuilds from nothing
        del ns["__pragma__"]
        del ns["__reader__"]
        self.assertIsNone(ns.get("__pragma__"))

        rebuilt = await expr()

        pragma = ns["__pragma__"]
        self.assertEqual(pragma.get("compiler/stray-colon"), "error")
        self.assertIsNot(pragma, pkg.__package_pragma__)
        self.assertEqual(rebuilt.name, "gcpkg.child")


    async def test_reader_rebuild_inherits_the_package_pragma(self):
        """
        find_module_reader seeds a missing __pragma__ from the nearest
        package too, since it is the fallback that runs first
        """

        pkg = types.ModuleType("gcpkg2")
        pkg.__package_pragma__ = ModulePragma(
            {"compiler/stray-colon": "error"})
        sys.modules["gcpkg2"] = pkg
        self.addCleanup(sys.modules.pop, "gcpkg2", None)

        ns = ModuleNamespace("gcpkg2.child", {})
        find_module_reader(ns)
        self.assertEqual(
            ns["__pragma__"].get("compiler/stray-colon"), "error")


    async def test_evaluate_after_detached_load(self):
        """
        a function in a loaded module can evaluate after the load-time
        facets were detached
        """

        src = """
        (define run (lambda [] (evaluate (+ 20 22))))
        """
        module = new_module("evaltest")
        ns = vars(module)
        ns['__file__'] = "<evaltest>"
        await load_module_source_async(
            module, dedent(src), seal=True, detach=True)

        # detaching empties the binding: the name stays bound but holds
        # no value, so it is out of the mapping
        self.assertNotIn('__compiler__', ns)
        self.assertIn('__compiler__', ns.refs)
        self.assertIsInstance(ns['__pragma__'], ModulePragma)

        self.assertEqual(demand_call(ns['run']), 42)
        self.assertIsInstance(ns['__compiler__'], Compiler)


# The end.
