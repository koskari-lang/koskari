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

Unit tests for koskari_scripts stub generation and install_scripts hook.
"""


import unittest

from koskari.extras.setuptools.scripts import (
    KoskariInstallScripts,
    iter_koskari_script_args,
    koskari_script_source,
)
from koskari.launcher import KOSKARI_SCRIPTS_GROUP

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


class TestKoskariScriptSource(unittest.TestCase):

    def test_emits_run_resolved_script_call(self):
        src = koskari_script_source(
            'myapp==1.0',
            KOSKARI_SCRIPTS_GROUP,
            'koslife')
        self.assertIn("run_resolved_script('myapp==1.0', 'koslife')", src)
        self.assertIn('KOSKARI-SCRIPT-ENTRY', src)


class TestIterKoskariScriptArgs(unittest.TestCase):

    def test_yields_koskari_stub_for_koskari_scripts_group(self):
        dist = FakeDist(FakeEntryPoints({
            KOSKARI_SCRIPTS_GROUP: [
                FakeEntryPoint(
                    'koslife', 'koslife.cli:main', KOSKARI_SCRIPTS_GROUP),
            ],
        }))

        scripts = {}
        for name, contents in iter_koskari_script_args(
                dist, header='#!python\n'):
            scripts[name] = contents

        self.assertIn('run_resolved_script', scripts['koslife'])
        self.assertIn("KOSKARI-SCRIPT-ENTRY: 'myapp==1.0'", scripts['koslife'])
        self.assertNotIn('load_entry_point', scripts['koslife'])

    def test_empty_when_no_koskari_scripts(self):
        dist = FakeDist(FakeEntryPoints({
            'console_scripts': [
                FakeEntryPoint('plain', 'plain.cli:main', 'console_scripts'),
            ],
        }))
        self.assertEqual(list(iter_koskari_script_args(dist)), [])


class TestKoskariInstallScripts(unittest.TestCase):

    def test_subclasses_setuptools_install_scripts(self):
        from setuptools.command.install_scripts import install_scripts
        self.assertTrue(issubclass(KoskariInstallScripts, install_scripts))


# The end.
