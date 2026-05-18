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

from koskari.importer import install, uninstall
from koskari.importer import KoskariSourceFileLoader
from koskari.serialize.incremental import compile_ki_file_to_kio

import pytest

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
