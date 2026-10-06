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
tests.importer.test_builtin_not_exported

A builtin a module's function calls is not a binding of that module:
it is not an attribute, not in dir() or vars(), and a star import from
the module, Python's or import-all-from, leaves the importer's own
binding of that name alone.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import builtins
import importlib
import os
import shutil
import sys
import tempfile
import unittest

import pytest

from koskari.importer import install, uninstall
from koskari.lib import demand


pytestmark = pytest.mark.phase_importer


class TestBuiltinNotExported(unittest.IsolatedAsyncioTestCase):

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_builtin_export_')
        self._prev_path = sys.path[:]
        sys.path.insert(0, self._tmpdir)

        pkg = os.path.join(self._tmpdir, 'pkg2')
        os.makedirs(pkg)
        with open(os.path.join(pkg, '__init__.py'), 'w', encoding='utf-8'):
            pass
        self.write(os.path.join(pkg, 'helpers.ki'),
                   "define biggest [xs]:\n"
                   "    (max xs)\n")
        self.write(os.path.join(pkg, 'user.ki'),
                   "define max [a b]:\n"
                   "    \"mine\"\n"
                   "\n"
                   "import-all-from pkg2.helpers\n"
                   "\n"
                   "define answer []:\n"
                   "    (max 1 2)\n")


    def tearDown(self):
        uninstall()
        install()
        sys.path[:] = self._prev_path
        for name in [n for n in sys.modules
                     if n == 'pkg2' or n.startswith('pkg2.')]:
            del sys.modules[name]
        shutil.rmtree(self._tmpdir, ignore_errors=True)


    def write(self, path, content):
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(content)


    async def test_builtin_used_by_a_function_is_not_an_attribute(self):
        h = importlib.import_module('pkg2.helpers')
        self.assertEqual(3, demand(h.biggest)([1, 3, 2]))
        self.assertFalse(hasattr(h, 'max'))
        self.assertNotIn('max', dir(h))
        self.assertNotIn('max', vars(h))


    async def test_python_star_import_leaves_the_importer_name(self):
        ns = {'max': 'python-side max'}
        exec("from pkg2.helpers import *", ns)
        self.assertEqual('python-side max', ns['max'])
        self.assertIsNot(builtins.max, ns['max'])


    async def test_import_all_from_leaves_the_importer_name(self):
        u = importlib.import_module('pkg2.user')
        self.assertEqual('mine', demand(u.answer)())


# The end.
