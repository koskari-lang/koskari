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
Integration tests for koskari.importer.importlib: install hooks, import
of temporary .ki files.
"""


from textwrap import dedent

import importlib
import importlib.util
import os
import sys
import tempfile
import types
import unittest

import pytest

from koskari.importer import (
    KoskariPathFinder, KoskariSourceFileLoader, install, is_installed,
    uninstall)


pytestmark = pytest.mark.phase_importer


class TestInstallUninstall(unittest.IsolatedAsyncioTestCase):

    def setUp(self):
        uninstall()

    def tearDown(self):
        uninstall()
        install()

    async def test_install_inserts_finder(self):
        self.assertFalse(is_installed())
        install()
        self.assertTrue(is_installed())

    async def test_install_at_front_of_meta_path(self):
        install()
        self.assertIs(sys.meta_path[0], KoskariPathFinder)

    async def test_double_install_is_noop(self):
        install()
        count_before = sys.meta_path.count(KoskariPathFinder)
        install()
        count_after = sys.meta_path.count(KoskariPathFinder)
        self.assertEqual(count_before, count_after)

    async def test_uninstall_removes_finder(self):
        install()
        self.assertTrue(is_installed())
        uninstall()
        self.assertFalse(is_installed())

    async def test_loader_class(self):
        install()
        finder = sys.meta_path[0]
        self.assertIs(finder, KoskariPathFinder)


class TestKiImport(unittest.IsolatedAsyncioTestCase):
    """
    Create a temporary .ki file and import it via importlib.import_module
    to verify that the whole pipeline works end-to-end.
    """

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_test_')
        self._prev_path = sys.path[:]
        sys.path.insert(0, self._tmpdir)


    def tearDown(self):
        uninstall()
        install()
        sys.path[:] = self._prev_path
        # Evict any modules loaded from the tmpdir
        to_remove = [
            name for name, mod in list(sys.modules.items())
            if hasattr(mod, '__file__')
            and mod.__file__
            and mod.__file__.startswith(self._tmpdir)
        ]
        for name in to_remove:
            del sys.modules[name]

        import shutil
        shutil.rmtree(self._tmpdir, ignore_errors=True)


    def _write_ki(self, modname, content):
        path = os.path.join(self._tmpdir, modname + '.ki')
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(dedent(content))
        return path


    async def test_import_empty_module(self):
        self._write_ki('ki_empty', '')
        mod = importlib.import_module('ki_empty')
        self.assertIsNotNone(mod)


    async def test_module_has_file_attribute(self):
        path = self._write_ki('ki_filattr', '')
        mod = importlib.import_module('ki_filattr')
        self.assertTrue(hasattr(mod, '__file__'))
        self.assertEqual(path, mod.__file__)


    async def test_define_binding_visible(self):
        self._write_ki('ki_defn', '(define answer 42)')
        mod = importlib.import_module('ki_defn')
        self.assertEqual(42, mod.answer)


    async def test_multiple_defines(self):
        self._write_ki('ki_multi', '(define x 1)\n(define y 2)')
        mod = importlib.import_module('ki_multi')
        self.assertEqual(1, mod.x)
        self.assertEqual(2, mod.y)


class TestExecModuleDemand(unittest.TestCase):
    """
    Test exec_module_demand directly (loader runs .ki via demand path).
    """

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_test_')
        self._prev_path = sys.path[:]
        sys.path.insert(0, self._tmpdir)

    def tearDown(self):
        uninstall()
        install()
        sys.path[:] = self._prev_path
        to_remove = [
            name for name, mod in list(sys.modules.items())
            if hasattr(mod, '__file__')
            and mod.__file__
            and mod.__file__.startswith(self._tmpdir)
        ]
        for name in to_remove:
            del sys.modules[name]
        import shutil
        shutil.rmtree(self._tmpdir, ignore_errors=True)

    def test_exec_module_demand_binds_define(self):
        path = os.path.join(self._tmpdir, 'demand_loader.ki')
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write('(define result 42)\n')
        loader = KoskariSourceFileLoader('demand_loader', path)
        spec = importlib.util.spec_from_file_location(
            'demand_loader', path, loader=loader)
        module = types.ModuleType('demand_loader')
        module.__name__ = 'demand_loader'
        module.__package__ = None
        module.__spec__ = spec
        loader.exec_module_demand(module)
        self.assertEqual(42, module.result)


# The end.
