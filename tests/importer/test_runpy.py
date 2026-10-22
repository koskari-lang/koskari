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

runpy and python -m run a Koskari module through the code object the
loader hands them: the module loads under the executing name, and under
__main__ its entry point runs and its status is the exit code. The
loader also answers is_package from the file name.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import importlib.util
import io
import os
import runpy
import shutil
import sys
import tempfile
import types
import unittest
from contextlib import redirect_stderr, redirect_stdout

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
        self._write('kimod.ki', 'print "ran as" __name__\n')
        self._write(
            'kientry.ki',
            'print "loaded" __name__\n'
            '\n'
            '@entry-point\n'
            'define main []:\n'
            '  print "entry" __name__\n'
            '  3\n')
        self._write(
            'kiboom.ki',
            '@entry-point\n'
            'define main []:\n'
            '  raise (ValueError "boom")\n')
        pkg = os.path.join(self._tmpdir, 'kpkg')
        os.mkdir(pkg)
        self._write(os.path.join('kpkg', '__init__.ki'), '(define answer 1)\n')
        self._write(
            os.path.join('kpkg', '__main__.ki'),
            '@entry-point\n'
            'define main []:\n'
            '  print "package" __package__\n'
            '  7\n')


    def tearDown(self):
        uninstall()
        install()
        sys.path[:] = self._prev_path
        for name in list(sys.modules):
            if name.split('.', 1)[0] in ('kimod', 'kientry', 'kiboom',
                                         'kpkg', 'kstale'):
                del sys.modules[name]
        shutil.rmtree(self._tmpdir, ignore_errors=True)


    def _write(self, relpath, text):
        path = os.path.join(self._tmpdir, relpath)
        with open(path, 'w') as fh:
            fh.write(text)
        return path


    def _run_as_main(self, name):
        """
        runpy.run_module under __main__, the path python -m takes, with
        the status it exits with and the captured stdout and stderr
        """

        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            with self.assertRaises(SystemExit) as caught:
                runpy.run_module(name, run_name='__main__', alter_sys=True)
        return caught.exception.code, out.getvalue(), err.getvalue()


    def test_get_code_is_a_python_shim(self):
        spec = importlib.util.find_spec('kimod')
        code = spec.loader.get_code('kimod')
        self.assertIsInstance(code, types.CodeType)
        self.assertEqual(spec.origin, code.co_filename)


    def test_run_as_main_runs_the_entry_point(self):
        status, out, err = self._run_as_main('kientry')
        self.assertEqual(3, status, err)
        self.assertEqual("loaded __main__\nentry __main__\n", out)


    def test_run_as_main_without_entry_point_exits_zero(self):
        status, out, err = self._run_as_main('kimod')
        self.assertEqual(0, status, err)
        self.assertEqual("ran as __main__\n", out)


    def test_run_as_main_restores_the_main_module(self):
        before = sys.modules['__main__']
        self._run_as_main('kientry')
        self.assertIs(before, sys.modules['__main__'])


    def test_run_as_main_renders_an_entry_exception(self):
        status, out, err = self._run_as_main('kiboom')
        self.assertEqual(1, status)
        self.assertIn("ValueError: boom", err)


    def test_package_runs_its_main_submodule(self):
        status, out, err = self._run_as_main('kpkg')
        self.assertEqual(7, status, err)
        self.assertEqual("package kpkg\n", out)


    def test_run_module_under_its_own_name_loads_without_the_entry(self):
        out = io.StringIO()
        with redirect_stdout(out):
            runpy.run_module('kientry')
        self.assertEqual("loaded kientry\n", out.getvalue())


    def test_run_module_under_its_own_name_leaves_sys_modules_alone(self):
        with redirect_stdout(io.StringIO()):
            runpy.run_module('kientry')
        self.assertNotIn('kientry', sys.modules)


    def test_stale_kio_runs_the_source(self):
        from koskari.serialize.incremental import compile_ki_file_to_kio

        src = self._write(
            'kstale.ki',
            '@entry-point\n'
            'define main []:\n'
            '  4\n')
        kio = os.path.join(self._tmpdir, 'kstale.kio')
        compile_ki_file_to_kio(src, kio)
        self._write(
            'kstale.ki',
            '@entry-point\n'
            'define main []:\n'
            '  5\n')
        later = os.stat(kio).st_mtime + 10
        os.utime(src, (later, later))
        status, out, err = self._run_as_main('kstale')
        self.assertEqual(5, status, err)


    def test_is_package_answers_from_the_file_name(self):
        mod = importlib.util.find_spec('kimod')
        self.assertFalse(mod.loader.is_package('kimod'))
        pkg = importlib.util.find_spec('kpkg')
        self.assertTrue(pkg.loader.is_package('kpkg'))


# The end.
