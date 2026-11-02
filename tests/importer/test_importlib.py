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
import importlib.metadata
import importlib.util
import os
import sys
import tempfile
import types
import unittest

import pytest

from koskari.importer import (
    KoskariSourceFileLoader, install, is_installed, uninstall)


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

    async def test_install_does_not_add_distributions(self):
        """
        The finder finds modules, not distribution metadata: installing
        it leaves importlib.metadata's view of the environment as it was
        """

        before = sorted(
            d.metadata['Name'] for d in importlib.metadata.distributions())
        install()
        after = sorted(
            d.metadata['Name'] for d in importlib.metadata.distributions())
        self.assertEqual(before, after)

    async def test_install_adds_nothing_to_meta_path(self):
        """
        Koskari modules are found by the per-directory finder Python's
        own PathFinder consults, so builtins, frozen modules and
        sys.path order keep Python's precedence
        """

        before = list(sys.meta_path)
        install()
        self.assertEqual(before, sys.meta_path)

    async def test_install_adds_one_path_hook(self):
        before = len(sys.path_hooks)
        install()
        self.assertEqual(before + 1, len(sys.path_hooks))
        install()
        self.assertEqual(before + 1, len(sys.path_hooks))

    async def test_uninstall_removes_finder(self):
        install()
        self.assertTrue(is_installed())
        uninstall()
        self.assertFalse(is_installed())

    async def test_directory_finder_knows_both_languages(self):
        """
        One finder serves a directory: it answers for .py and for .ki,
        with the Koskari loader on the .ki spec
        """

        install()
        with tempfile.TemporaryDirectory() as tmp:
            with open(os.path.join(tmp, 'both_py.py'), 'w') as fh:
                fh.write('x = 1\n')
            with open(os.path.join(tmp, 'both_ki.ki'), 'w') as fh:
                fh.write('(var x 2)\n')
            finder = importlib.import_module('importlib.machinery') \
                .PathFinder._path_importer_cache(tmp)
            py_spec = finder.find_spec('both_py')
            ki_spec = finder.find_spec('both_ki')
        self.assertIsNotNone(py_spec)
        self.assertIsNotNone(ki_spec)
        self.assertIsInstance(ki_spec.loader, KoskariSourceFileLoader)
        self.assertNotIsInstance(py_spec.loader, KoskariSourceFileLoader)


class TestPrecedence(unittest.IsolatedAsyncioTestCase):
    """
    A .ki module obeys Python's import precedence: sys.path order
    between directories, builtins ahead of every directory
    """

    def setUp(self):
        uninstall()
        install()
        self._prev_path = sys.path[:]
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_prec_')

    def tearDown(self):
        sys.path[:] = self._prev_path
        for name in ('prec_order', 'pwd'):
            sys.modules.pop(name, None)
        import shutil
        shutil.rmtree(self._tmpdir, ignore_errors=True)
        uninstall()
        install()
        importlib.invalidate_caches()

    async def test_earlier_py_beats_later_ki(self):
        first = os.path.join(self._tmpdir, 'first')
        second = os.path.join(self._tmpdir, 'second')
        os.makedirs(first)
        os.makedirs(second)
        with open(os.path.join(first, 'prec_order.py'), 'w') as fh:
            fh.write("which = 'py'\n")
        with open(os.path.join(second, 'prec_order.ki'), 'w') as fh:
            fh.write('(var which "ki")\n')
        sys.path[:0] = [first, second]
        importlib.invalidate_caches()

        mod = importlib.import_module('prec_order')
        self.assertEqual('py', mod.which)

    async def test_ki_does_not_shadow_a_builtin(self):
        with open(os.path.join(self._tmpdir, 'pwd.ki'), 'w') as fh:
            fh.write('(var shadowed True)\n')
        sys.path.insert(0, self._tmpdir)
        sys.modules.pop('pwd', None)
        importlib.invalidate_caches()

        mod = importlib.import_module('pwd')
        self.assertEqual('built-in', mod.__spec__.origin)
        self.assertFalse(hasattr(mod, 'shadowed'))


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
        self._write_ki('ki_defn', '(var answer 42)')
        mod = importlib.import_module('ki_defn')
        self.assertEqual(42, mod.answer)


    async def test_multiple_defines(self):
        self._write_ki('ki_multi', '(var x 1)\n(var y 2)')
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
            fh.write('(var result 42)\n')
        loader = KoskariSourceFileLoader('demand_loader', path)
        spec = importlib.util.spec_from_file_location(
            'demand_loader', path, loader=loader)
        module = importlib.util.module_from_spec(spec)
        loader.exec_module_demand(module)
        self.assertEqual(42, module.result)


# The end.
