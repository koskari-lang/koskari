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
Tests for the package pragma: the (package-pragma ...) surface macro,
loader seeding of a module's __pragma__ from the nearest ancestor
package's __package_pragma__, and end-to-end inheritance through
on-disk .ki package trees.
"""


import importlib
import os
import shutil
import sys
import tempfile
import types
import unittest

import pytest

from koskari.compile.pragma import BracketCalls
from koskari.importer import install, uninstall
from koskari.lib import Module, ModuleNamespace
from koskari.module import build_module_load_facets, new_module
from koskari.pragma import ModulePragma
from koskari.reader import default_reader, source_str


@pytest.mark.phase_basics
class TestPackagePragmaMacro(unittest.IsolatedAsyncioTestCase):
    """
    The (package-pragma ...) surface macro writes both __pragma__ and
    __package_pragma__ when given settings; the bare and single-key
    forms report, mirroring (pragma)
    """

    def make_module(self):
        import koskari.builtins as builtins

        mod_ns = ModuleNamespace("test", dict(vars(builtins)))
        module = Module(mod_ns)
        compiler, _reader = build_module_load_facets(module)
        return module, compiler


    def compile_expr(self, module, compiler, src_str):
        source = default_reader.read(source_str(src_str, __file__))
        fragment = compiler.full_compile(source)
        return fragment.as_expression(vars(module))


    async def test_sets_both_slots(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler, "(package-pragma [compiler/tco-enable False])")
        await expr()

        mvars = vars(module)
        self.assertIs(mvars["__package_pragma__"], mvars["__pragma__"])
        self.assertEqual(
            False, mvars["__package_pragma__"].get("compiler/tco-enable"))


    async def test_empty_form_reports_unset(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(module, compiler, "(package-pragma)")
        result = await expr()

        self.assertIsNone(result)


    async def test_empty_form_reports_recorded(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler,
            "(begin (package-pragma [compiler/tco-enable False])"
            " (package-pragma))")
        result = await expr()

        self.assertIs(result, vars(module)["__package_pragma__"])


    async def test_later_local_override_preserves_default(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler,
            "(begin (package-pragma [compiler/tco-enable False])"
            " (pragma [compiler/call-brackets 'error]))")
        await expr()

        mvars = vars(module)
        local = mvars["__pragma__"]
        default = mvars["__package_pragma__"]

        self.assertIsNot(local, default)
        self.assertEqual(BracketCalls.ERROR,
                         local.get("compiler/call-brackets"))
        self.assertEqual(BracketCalls.WARN,
                         default.get("compiler/call-brackets"))
        self.assertEqual(False, local.get("compiler/tco-enable"))
        self.assertEqual(False, default.get("compiler/tco-enable"))


    async def test_accumulates_across_declarations(self):
        module, compiler = self.make_module()
        expr = self.compile_expr(
            module, compiler,
            "(begin (package-pragma [compiler/tco-enable False])"
            " (package-pragma [compiler/call-brackets 'error]))")
        await expr()

        default = vars(module)["__package_pragma__"]
        self.assertEqual(False, default.get("compiler/tco-enable"))
        self.assertEqual(BracketCalls.ERROR,
                         default.get("compiler/call-brackets"))


@pytest.mark.phase_importer
class TestPackagePragmaSeeding(unittest.IsolatedAsyncioTestCase):
    """
    build_module_load_facets seeds a fresh module pragma from the nearest
    ancestor package's __package_pragma__
    """

    PARENT = "kospkgt_parent"


    def setUp(self):
        self._fakes = []


    def tearDown(self):
        for name in self._fakes:
            sys.modules.pop(name, None)


    def fake_package(self, name, pragma=None):
        parent = types.ModuleType(name)
        if pragma is not None:
            parent.__package_pragma__ = pragma
        sys.modules[name] = parent
        self._fakes.append(name)
        return parent


    def make_child(self, name, package=False):
        module = new_module(name)
        if package:
            vars(module)['__path__'] = []
        return module


    async def test_child_seeds_from_parent_package(self):
        inherited = ModulePragma({"compiler/tco-enable": False})
        self.fake_package(self.PARENT, inherited)

        module = self.make_child(f"{self.PARENT}.child")
        build_module_load_facets(module)

        pragma = vars(module)["__pragma__"]
        self.assertIsNot(pragma, inherited)
        self.assertEqual(False, pragma.get("compiler/tco-enable"))


    async def test_leaf_child_gets_no_package_slot(self):
        self.fake_package(
            self.PARENT, ModulePragma({"compiler/tco-enable": False}))

        module = self.make_child(f"{self.PARENT}.child")
        build_module_load_facets(module)

        self.assertIsNone(vars(module).get("__package_pragma__"))


    async def test_package_child_stamps_inherited_copy(self):
        inherited = ModulePragma({"compiler/tco-enable": False})
        self.fake_package(self.PARENT, inherited)

        module = self.make_child(f"{self.PARENT}.sub", package=True)
        build_module_load_facets(module)

        stamped = vars(module)["__package_pragma__"]
        self.assertIsNot(stamped, inherited)
        self.assertEqual(False, stamped.get("compiler/tco-enable"))


    async def test_walk_skips_slotless_parent(self):
        self.fake_package(
            self.PARENT, ModulePragma({"compiler/tco-enable": False}))
        self.fake_package(f"{self.PARENT}.mid")

        module = self.make_child(f"{self.PARENT}.mid.child")
        build_module_load_facets(module)

        pragma = vars(module)["__pragma__"]
        self.assertEqual(False, pragma.get("compiler/tco-enable"))


    async def test_no_ancestor_slot_uses_default(self):
        self.fake_package(self.PARENT)

        module = self.make_child(f"{self.PARENT}.child")
        build_module_load_facets(module)

        pragma = vars(module)["__pragma__"]
        self.assertEqual(True, pragma.get("compiler/tco-enable"))


    async def test_parent_watchers_stay_empty(self):
        inherited = ModulePragma({"compiler/tco-enable": False})
        self.fake_package(self.PARENT, inherited)

        module = self.make_child(f"{self.PARENT}.child")
        build_module_load_facets(module)

        self.assertEqual(0, len(inherited._watchers))


    async def test_compiler_derives_from_seed(self):
        self.fake_package(
            self.PARENT, ModulePragma({"compiler/convert-fast": False}))

        module = self.make_child(f"{self.PARENT}.child")
        compiler, _reader = build_module_load_facets(module)

        self.assertEqual(False, compiler.convert_fast_enabled)


@pytest.mark.phase_importer
class TestPackagePragmaImport(unittest.IsolatedAsyncioTestCase):
    """
    Package pragma inheritance through real .ki package trees imported
    via the koskari import hooks
    """

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_test_')
        self._prev_path = sys.path[:]
        sys.path.insert(0, self._tmpdir)


    def tearDown(self):
        uninstall()
        sys.path[:] = self._prev_path
        to_remove = [
            name for name, mod in list(sys.modules.items())
            if hasattr(mod, '__file__')
            and mod.__file__
            and mod.__file__.startswith(self._tmpdir)
        ]
        for name in to_remove:
            del sys.modules[name]
        shutil.rmtree(self._tmpdir, ignore_errors=True)


    def _write(self, relpath, content):
        path = os.path.join(self._tmpdir, relpath)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(content)
        return path


    def _write_tree(self):
        self._write(
            'kospkgp/__init__.ki',
            "(package-pragma [compiler/tco-enable False])\n")
        self._write(
            'kospkgp/mod.ki',
            "(define seen (pragma compiler/tco-enable))\n")
        self._write('kospkgp/sub/__init__.ki', "\n")
        self._write(
            'kospkgp/sub/deep.ki',
            "(define seen (pragma compiler/tco-enable))\n")
        self._write(
            'kospkgp/sub2/__init__.ki',
            "(package-pragma [compiler/call-brackets 'error])\n")
        self._write(
            'kospkgp/sub2/deep.ki',
            "(define seen-tco (pragma compiler/tco-enable))\n"
            "(define seen-brackets (pragma compiler/call-brackets))\n")


    async def test_package_slot_survives_load(self):
        self._write_tree()
        pkg = importlib.import_module('kospkgp')
        default = pkg.__package_pragma__
        self.assertEqual(False, default.get("compiler/tco-enable"))


    async def test_submodule_inherits(self):
        self._write_tree()
        mod = importlib.import_module('kospkgp.mod')
        self.assertIs(False, mod.seen)


    async def test_silent_subpackage_pulls_down(self):
        self._write_tree()
        deep = importlib.import_module('kospkgp.sub.deep')
        self.assertIs(False, deep.seen)

        sub = sys.modules['kospkgp.sub']
        stamped = sub.__package_pragma__
        self.assertEqual(False, stamped.get("compiler/tco-enable"))


    async def test_subpackage_refines_default(self):
        self._write_tree()
        deep = importlib.import_module('kospkgp.sub2.deep')
        self.assertIs(False, getattr(deep, 'seen-tco'))
        self.assertEqual(
            BracketCalls.ERROR, getattr(deep, 'seen-brackets'))


    async def test_unrelated_module_uses_default(self):
        self._write_tree()
        self._write(
            'kossolo.ki',
            "(define seen (pragma compiler/tco-enable))\n")
        solo = importlib.import_module('kossolo')
        self.assertIs(True, solo.seen)


# The end.
