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
tests.importer.test_module_is_module_type

An imported .ki module is a types.ModuleType: the stdlib tools that
test for one (inspect, doctest, pydoc) accept it, its repr names the
module and its file, and its __dict__ is the namespace the module's
own functions read and write.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import doctest
import importlib
import inspect
import os
import pydoc
import shutil
import sys
import tempfile
import types
import unittest

import pytest

from koskari.importer import install, uninstall
from koskari.lib import demand


pytestmark = pytest.mark.phase_importer


SRC = '''"""
A module with a docstring and a doctest.

>>> twice(4)
8
"""

@demand
def twice [x]:
    (* x 2)
'''


class TestModuleIsModuleType(unittest.IsolatedAsyncioTestCase):

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_module_type_')
        self._prev_path = sys.path[:]
        sys.path.insert(0, self._tmpdir)
        self.path = os.path.join(self._tmpdir, 'plainmod.ki')
        with open(self.path, 'w', encoding='utf-8') as fh:
            fh.write(SRC)
        sys.modules.pop('plainmod', None)
        self.mod = importlib.import_module('plainmod')


    def tearDown(self):
        uninstall()
        install()
        sys.path[:] = self._prev_path
        sys.modules.pop('plainmod', None)
        shutil.rmtree(self._tmpdir, ignore_errors=True)


    async def test_is_a_module(self):
        self.assertIsInstance(self.mod, types.ModuleType)
        self.assertTrue(inspect.ismodule(self.mod))


    async def test_repr_names_module_and_file(self):
        text = repr(self.mod)
        self.assertIn("<module 'plainmod'", text)
        self.assertIn(self.path, text)


    async def test_inspect_getfile(self):
        self.assertEqual(self.path, inspect.getfile(self.mod))


    async def test_doctest_runs(self):
        failed, attempted = doctest.testmod(self.mod)
        self.assertEqual((0, 1), (failed, attempted))


    async def test_pydoc_describes_the_module(self):
        text = pydoc.render_doc(self.mod, renderer=pydoc.plaintext)
        self.assertIn('module plainmod', text)
        self.assertIn('A module with a docstring', text)
        self.assertIn(self.path, text)


    async def test_dict_is_the_namespace(self):
        self.assertIs(vars(self.mod), self.mod.__dict__)
        self.assertEqual(8, demand(vars(self.mod)['twice'])(4))


# The end.
