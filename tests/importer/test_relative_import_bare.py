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
tests.importer.test_relative_import_bare

A relative import written as dots alone names an enclosing package:
(import [.. as: parent]) binds the parent package, and import-from ..
binds a sibling from it, as Python's from .. import does.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import importlib
import os
import shutil
import sys
import tempfile
import unittest

import pytest

from koskari.importer import install, uninstall


pytestmark = pytest.mark.phase_importer


class TestBareRelativeImport(unittest.IsolatedAsyncioTestCase):

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_bare_relative_')
        self._prev_path = sys.path[:]
        sys.path.insert(0, self._tmpdir)

        pkg = os.path.join(self._tmpdir, 'rp')
        sub = os.path.join(pkg, 'sub')
        os.makedirs(sub)
        for init in (os.path.join(pkg, '__init__.py'),
                     os.path.join(sub, '__init__.py')):
            with open(init, 'w', encoding='utf-8'):
                pass
        self.write(os.path.join(pkg, 'sib.ki'), "define SIB 7\n")
        self.write(os.path.join(sub, 'bare2.ki'),
                   "(import [.. as: parent_pkg])\n")
        self.write(os.path.join(sub, 'twodot.ki'),
                   "import-from .. sib\n")


    def tearDown(self):
        uninstall()
        install()
        sys.path[:] = self._prev_path
        for name in [n for n in sys.modules if n == 'rp' or n.startswith('rp.')]:
            del sys.modules[name]
        shutil.rmtree(self._tmpdir, ignore_errors=True)


    def write(self, path, content):
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(content)


    async def test_bare_two_dot_import_binds_the_parent_package(self):
        mod = importlib.import_module('rp.sub.bare2')
        self.assertEqual('rp', mod.parent_pkg.__name__)


    async def test_two_dot_import_from_binds_a_sibling(self):
        mod = importlib.import_module('rp.sub.twodot')
        self.assertEqual(7, mod.sib.SIB)


# The end.
