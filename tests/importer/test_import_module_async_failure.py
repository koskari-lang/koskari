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
tests.importer.test_import_module_async_failure

A module whose body raises during import_module_async leaves nothing
behind in sys.modules, so a later import tries again instead of handing
back the half-loaded module.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import importlib
import os
import shutil
import sys
import tempfile
import unittest

import pytest

from koskari.importer import (
    KoskariPathFinder, import_module_async, install, uninstall)


pytestmark = pytest.mark.phase_importer


class TestImportModuleAsyncFailure(unittest.IsolatedAsyncioTestCase):

    def setUp(self):
        uninstall()
        install()
        self.tmpdir = tempfile.mkdtemp(prefix='koskari_import_fail_')
        self.prev_path = sys.path[:]
        sys.path.insert(0, self.tmpdir)

    def tearDown(self):
        sys.modules.pop('failing_body', None)
        sys.path[:] = self.prev_path
        shutil.rmtree(self.tmpdir, ignore_errors=True)
        uninstall()
        install()

    def write(self, source):
        path = os.path.join(self.tmpdir, 'failing_body.ki')
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(source)
        importlib.invalidate_caches()
        KoskariPathFinder.invalidate_caches()

    async def test_failed_load_leaves_no_module_behind(self):
        self.write('(define before 1)\n(raise (RuntimeError "body failed"))\n')

        with self.assertRaises(RuntimeError):
            await import_module_async('failing_body')
        self.assertNotIn('failing_body', sys.modules)

        with self.assertRaises(RuntimeError):
            await import_module_async('failing_body')
        self.assertNotIn('failing_body', sys.modules)

        self.write('(define before 2)\n')
        module = await import_module_async('failing_body')
        self.assertEqual(module.before, 2)
        self.assertIs(sys.modules['failing_body'], module)


# The end.
