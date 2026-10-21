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
tests.importer.test_import_root_parameter

A .ki module imported under plain Python, with no with Domain() open,
stores a parameter at top level into the thread's default Domain: the
value is read afterwards from Python and by a module imported later.

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
from koskari.lib import ParamRef


pytestmark = pytest.mark.phase_importer


class TestImportRootParameter(unittest.TestCase):

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_rootparam_')
        self._prev_path = sys.path[:]
        sys.path.insert(0, self._tmpdir)


    def tearDown(self):
        uninstall()
        install()
        sys.path[:] = self._prev_path
        for name in [n for n in sys.modules if n.startswith('ki_rootparam_')]:
            del sys.modules[name]
        shutil.rmtree(self._tmpdir, ignore_errors=True)


    def write_ki(self, modname, content):
        path = os.path.join(self._tmpdir, modname + '.ki')
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(dedent(content))
        return path


    def test_top_level_store_is_read_from_python(self):
        self.write_ki('ki_rootparam_store', """
        (define-parameter ki_rootparam_greeting)
        (set! ki_rootparam_greeting "hello")
        """)
        self.write_ki('ki_rootparam_read', """
        (define seen (parameterq ki_rootparam_greeting None))
        """)
        ref = ParamRef('ki_rootparam_greeting')
        self.addCleanup(ref.clear)

        importlib.import_module('ki_rootparam_store')
        self.assertEqual(ref.deref(), "hello")

        reader = importlib.import_module('ki_rootparam_read')
        self.assertEqual(reader.seen, "hello")


# The end.
