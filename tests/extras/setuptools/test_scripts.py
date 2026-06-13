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
tests.extras.setuptools.test_scripts

Unit tests for koskari_scripts ScriptWriter customization.
"""


import unittest

from koskari.extras.setuptools.scripts import (
    KoskariScriptWriter,
    KoskariWindowsExecutableLauncherWriter,
    KoskariWindowsScriptWriter,
    ensure_koskari_script_writer,
    koskari_script_names,
)
from koskari.launcher import KOSKARI_SCRIPTS_GROUP, MIRROR_CONSOLE_TARGET

import pytest

pytestmark = pytest.mark.phase_extras


class FakeEntryPoint(object):

    def __init__(self, name, value, group):
        self.name = name
        self.value = value
        self.group = group


class FakeEntryPoints(object):

    def __init__(self, groups):
        self._groups = groups

    def select(self, group=None, name=None):
        if group is None:
            eps = []
            for items in self._groups.values():
                eps.extend(items)
        else:
            eps = list(self._groups.get(group, []))
        if name is not None:
            eps = [ep for ep in eps if ep.name == name]
        return eps


class FakeDist(object):

    def __init__(self, entry_points, name='myapp', version='1.0'):
        self.name = name
        self.version = version
        self.entry_points = entry_points


class TestKoskariScriptNames(unittest.TestCase):

    def test_collects_koskari_scripts_names(self):
        dist = FakeDist(None)
        dist.entry_points = {
            'koskari_scripts': ['koslife = koslife.cli:main'],
        }
        self.assertEqual(koskari_script_names(dist), {'koslife'})


class TestKoskariScriptWriter(unittest.TestCase):

    def setUp(self):
        ensure_koskari_script_writer()
        self.writer = KoskariScriptWriter

    def test_koskari_command_emits_run_resolved_script_stub(self):
        dist = FakeDist(FakeEntryPoints({
            'console_scripts': [
                FakeEntryPoint('koslife', MIRROR_CONSOLE_TARGET, 'console_scripts'),
                FakeEntryPoint('plain', 'plain.cli:main', 'console_scripts'),
            ],
            KOSKARI_SCRIPTS_GROUP: [
                FakeEntryPoint('koslife', 'koslife.cli:main', KOSKARI_SCRIPTS_GROUP),
            ],
        }))

        scripts = {}
        for args in self.writer.get_args(dist, header='#!python\n'):
            if len(args) == 2:
                name, contents = args
            else:
                name, contents = args[0], args[1]
            scripts[name] = contents

        self.assertIn('run_resolved_script', scripts['koslife'])
        self.assertIn("KOSKARI-SCRIPT-ENTRY: 'myapp==1.0'", scripts['koslife'])
        self.assertIn('load_entry_point', scripts['plain'])

    def test_patch_swaps_setuptools_script_writers(self):
        ensure_koskari_script_writer()
        from setuptools import _scripts
        self.assertIs(_scripts.ScriptWriter, KoskariScriptWriter)
        self.assertIs(_scripts.WindowsScriptWriter, KoskariWindowsScriptWriter)
        self.assertIs(
            _scripts.WindowsExecutableLauncherWriter,
            KoskariWindowsExecutableLauncherWriter)

    def test_patch_is_idempotent(self):
        ensure_koskari_script_writer()
        from setuptools import _scripts
        self.assertIs(_scripts.ScriptWriter, KoskariScriptWriter)
        ensure_koskari_script_writer()
        self.assertIs(_scripts.ScriptWriter, KoskariScriptWriter)

    def test_windows_mixin_combines_koskari_and_launcher_behaviour(self):
        stock_launcher = KoskariWindowsExecutableLauncherWriter.__bases__[1]
        mro = KoskariWindowsExecutableLauncherWriter.__mro__
        self.assertIn(KoskariScriptWriter, mro)
        self.assertIn(stock_launcher, mro)
        self.assertIs(
            KoskariWindowsExecutableLauncherWriter._get_script_args.__func__,
            stock_launcher._get_script_args.__func__)
        self.assertIs(
            KoskariWindowsExecutableLauncherWriter.get_args.__func__,
            KoskariScriptWriter.get_args.__func__)


# The end.
