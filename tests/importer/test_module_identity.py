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
tests.importer.test_module_identity

The module the importer places in sys.modules is the namespace the
module's own code runs in. A global a module function rebinds is what
Python reads back, an attribute Python assigns is what the module's
functions call, and a circular import sees the names defined before
the import point rather than an empty module.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from textwrap import dedent

import importlib
import os
import shutil
import sys
import tempfile
import unittest

import pytest

from koskari.importer import install, uninstall


pytestmark = pytest.mark.phase_importer


class TestModuleIdentity(unittest.IsolatedAsyncioTestCase):

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_identity_')
        self._prev_path = sys.path[:]
        sys.path.insert(0, self._tmpdir)


    def tearDown(self):
        uninstall()
        install()
        sys.path[:] = self._prev_path
        for name in [n for n in sys.modules if n.startswith('ki_ident_')]:
            del sys.modules[name]
        shutil.rmtree(self._tmpdir, ignore_errors=True)


    def write_ki(self, modname, content):
        path = os.path.join(self._tmpdir, modname + '.ki')
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(dedent(content))
        return path


    async def test_rebound_global_is_read_from_python(self):
        self.write_ki('ki_ident_counter', """
        (define count 0)
        (define bump (lambda [] (define-global count (+ count 1)) count))
        """)
        mod = importlib.import_module('ki_ident_counter')
        self.assertEqual(await mod.bump(), 1)
        self.assertEqual(mod.count, 1)


    async def test_attribute_assigned_from_python_is_called(self):
        self.write_ki('ki_ident_helper', """
        (define helper (lambda [] "real"))
        (define call-helper (lambda [] (helper)))
        """)
        mod = importlib.import_module('ki_ident_helper')
        mod.helper = lambda: "patched"
        self.assertEqual(await vars(mod)['call-helper'](), "patched")


    async def test_circular_import_sees_names_defined_so_far(self):
        self.write_ki('ki_ident_a', """
        (define x 1)
        (import ki_ident_b)
        (define y ki_ident_b.seen)
        """)
        self.write_ki('ki_ident_b', """
        (import-from ki_ident_a x)
        (define seen x)
        """)
        mod = importlib.import_module('ki_ident_a')
        self.assertEqual(mod.y, 1)


# The end.
