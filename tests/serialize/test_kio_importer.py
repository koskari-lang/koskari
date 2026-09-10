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
Importer path for precompiled .kio modules.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import importlib.util
import os
import tempfile
import types
import unittest

import pytest

from koskari.importer import KoskariSourceFileLoader, install, uninstall
from koskari.serialize.incremental import compile_ki_file_to_kio


pytestmark = pytest.mark.phase_importer


class TestKioImporterParity(unittest.TestCase):
    """
    .ki compile to .kio then exec matches direct .ki semantics for a define
    """

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_kio_')
        self._prev_path = __import__('sys').path[:]
        __import__('sys').path.insert(0, self._tmpdir)


    def tearDown(self):
        uninstall()
        install()
        __import__('sys').path[:] = self._prev_path
        to_remove = [
            name for name, mod in list(__import__('sys').modules.items())
            if hasattr(mod, '__file__')
            and mod.__file__
            and mod.__file__.startswith(self._tmpdir)
        ]
        for name in to_remove:
            del __import__('sys').modules[name]
        import shutil
        shutil.rmtree(self._tmpdir, ignore_errors=True)


    def _write(self, name, source):
        path = os.path.join(self._tmpdir, name)
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(source)
        return path


    def _load(self, modname, path):
        loader = KoskariSourceFileLoader(modname, path)
        spec = importlib.util.spec_from_file_location(
            modname, path, loader=loader)
        module = types.ModuleType(modname)
        module.__name__ = modname
        module.__package__ = None
        module.__spec__ = spec
        loader.exec_module_demand(module)
        return module


    def _both(self, stem, source):
        """
        Load ``source`` once from .ki and once from a .kio compiled from
        it; return the (ki module, kio module) pair
        """

        ki_path = self._write(stem + '_src.ki', source)
        kio_path = os.path.join(self._tmpdir, stem + '_out.kio')
        compile_ki_file_to_kio(ki_path, kio_path)
        return (self._load(stem + '_ki', ki_path),
                self._load(stem + '_kio', kio_path))


    def test_docstring_via_kio_matches_ki(self):
        ki, kio = self._both('doc', '"""the module doc"""\n(define x 1)\n')
        self.assertEqual(ki.__doc__, "the module doc")
        self.assertEqual(kio.__doc__, ki.__doc__)
        self.assertEqual(kio.x, 1)


    def test_pragma_via_kio_matches_ki(self):
        source = (
            "(pragma [compiler/call-brackets 'warn])\n"
            "(define setting (pragma compiler/call-brackets))\n")
        ki, kio = self._both('pragma', source)
        self.assertEqual(kio.setting, ki.setting)
        self.assertIsNotNone(kio.__pragma__)
        self.assertEqual(
            kio.__pragma__.get('compiler/call-brackets'),
            ki.__pragma__.get('compiler/call-brackets'))


    def test_exports_via_kio_matches_ki(self):
        source = "(declare-exports alpha)\n(define alpha 1)\n(define beta 2)\n"
        ki, kio = self._both('exports', source)
        self.assertEqual(ki.__all__, ('alpha',))
        self.assertEqual(kio.__all__, ki.__all__)


    def test_ellipsis_const_via_kio(self):
        _ki, kio = self._both('ellipsis', "(define e ...)\n")
        self.assertIs(kio.e, ...)


    def test_kio_bodies_keep_source_code_settings(self):
        """
        top-level bodies compile with the pragma defaults, no cache slots
        and no read-only sealing, and load back the same way
        """

        from koskari.serialize import load_kio

        ki_path = self._write('settings_src.ki', "(define a 1)\n(define b 2)\n")
        kio_path = os.path.join(self._tmpdir, 'settings_out.kio')
        compile_ki_file_to_kio(ki_path, kio_path)
        codes = load_kio(kio_path).codes
        self.assertEqual(len(codes), 2)
        for code in codes:
            self.assertFalse(code.caches)
            self.assertFalse(code.protected)


    def test_define_via_kio_matches_ki(self):
        ki_path = os.path.join(self._tmpdir, 'parity_src.ki')
        with open(ki_path, 'w', encoding='utf-8') as fh:
            fh.write('(define parity_answer 42)\n')

        kio_path = os.path.join(self._tmpdir, 'parity_out.kio')
        compile_ki_file_to_kio(ki_path, kio_path)

        loader = KoskariSourceFileLoader('parity_kio', kio_path)
        spec = importlib.util.spec_from_file_location(
            'parity_kio', kio_path, loader=loader)
        module = types.ModuleType('parity_kio')
        module.__name__ = 'parity_kio'
        module.__package__ = None
        module.__spec__ = spec
        loader.exec_module_demand(module)
        self.assertEqual(42, module.parity_answer)


# The end.
