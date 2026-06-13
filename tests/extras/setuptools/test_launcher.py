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
tests.extras.setuptools.test_launcher

Unit tests for koskari.launcher hosted script dispatch.
"""


import os
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

from koskari.importer import install, uninstall
from koskari.launcher import (
    EntryNotFoundError,
    EntryTargetError,
    hosted_run,
    parse_entry_target,
    run_entry,
    run_target,
    script_name_from_argv,
)

import pytest

pytestmark = pytest.mark.phase_extras


class TestLauncherHelpers(unittest.TestCase):

    def test_script_name_from_argv_strips_script_suffix(self):
        self.assertEqual(
            script_name_from_argv(['/usr/bin/koslife-script.py']),
            'koslife')

    def test_script_name_from_argv_strips_exe_suffix(self):
        self.assertEqual(
            script_name_from_argv(['C:\\Tools\\koslife.exe']),
            'koslife')

    def test_parse_entry_target(self):
        self.assertEqual(
            parse_entry_target('koslife.cli:main'),
            ('koslife.cli', 'main'))

    def test_parse_entry_target_rejects_bare_module(self):
        with self.assertRaises(EntryTargetError):
            parse_entry_target('koslife.cli')


class TestHostedRun(unittest.IsolatedAsyncioTestCase):

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_launcher_')
        self._prev_path = sys.path[:]
        sys.path.insert(0, self._tmpdir)
        self._loaded = []

    def tearDown(self):
        uninstall()
        sys.path[:] = self._prev_path
        for name in self._loaded:
            sys.modules.pop(name, None)
        shutil.rmtree(self._tmpdir, ignore_errors=True)

    def _write_ki(self, modname, content):
        path = os.path.join(self._tmpdir, modname + '.ki')
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(content)
        return path

    async def test_hosted_run_invokes_main(self):
        self._write_ki('ki_launcher_main', '(define main [] 42)')
        code = await hosted_run('ki_launcher_main', 'main', 'ki-tool', [])
        self.assertEqual(code, 42)

    async def test_hosted_run_sets_sys_argv(self):
        self._write_ki(
            'ki_launcher_argv',
            '(define main [argv] (len argv))')
        code = await hosted_run(
            'ki_launcher_argv', 'main', 'ki-tool', ['--flag'])
        self.assertEqual(code, 2)

    def test_run_target_against_ki_module(self):
        self._write_ki('ki_launcher_run', '(define main [] 7)')
        self._loaded.append('ki_launcher_run')
        code = run_target('ki_launcher_run:main', argv=[])
        self.assertEqual(code, 7)

    def test_run_entry_resolves_metadata(self):
        self._write_ki('ki_launcher_entry', '(define main [] 11)')
        self._loaded.append('ki_launcher_entry')

        class FakeEntry(object):
            name = 'ki-tool'
            value = 'ki_launcher_entry:main'

        with patch('koskari.launcher.metadata.entry_points', return_value=[FakeEntry()]):
            code = run_entry('ki-tool', argv=[])
        self.assertEqual(code, 11)

    def test_run_entry_missing_metadata_raises(self):
        with patch('koskari.launcher.metadata.entry_points', return_value=[]):
            with self.assertRaises(EntryNotFoundError):
                run_entry('missing-tool', argv=[])


# The end.
