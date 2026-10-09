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
tests.importer.test_runpy

runpy and python -m stop at a Koskari module with an ImportError that
names the launcher, instead of compiling the source as Python, and the
loader answers is_package from the file name.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import importlib.util
import os
import runpy
import shutil
import sys
import tempfile
import unittest

import pytest

from koskari.importer import install, uninstall


pytestmark = pytest.mark.phase_importer


class TestRunpy(unittest.TestCase):

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_test_')
        self._prev_path = sys.path[:]
        sys.path.insert(0, self._tmpdir)
        with open(os.path.join(self._tmpdir, 'kimod.ki'), 'w') as fh:
            fh.write('print "ran as" __name__\n')
        pkg = os.path.join(self._tmpdir, 'kpkg')
        os.mkdir(pkg)
        with open(os.path.join(pkg, '__init__.ki'), 'w') as fh:
            fh.write('(define answer 1)\n')


    def tearDown(self):
        uninstall()
        install()
        sys.path[:] = self._prev_path
        for name in ('kimod', 'kpkg'):
            sys.modules.pop(name, None)
        shutil.rmtree(self._tmpdir, ignore_errors=True)


    def test_run_module_raises_import_error_naming_the_launcher(self):
        # runpy re-raises the loader's ImportError with only its message
        with self.assertRaises(ImportError) as caught:
            runpy.run_module('kimod')
        message = str(caught.exception)
        self.assertIn("kimod is a Koskari module", message)
        self.assertIn("run it with koskari -m kimod", message)


    def test_is_package_answers_from_the_file_name(self):
        mod = importlib.util.find_spec('kimod')
        self.assertFalse(mod.loader.is_package('kimod'))
        pkg = importlib.util.find_spec('kpkg')
        self.assertTrue(pkg.loader.is_package('kpkg'))


# The end.
