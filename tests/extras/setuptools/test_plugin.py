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
tests.extras.setuptools.test_plugin

Unit tests for koskari_scripts setuptools plugin integration.
"""


import os
import sys
import tempfile
import types
import unittest
import warnings
from importlib import import_module

import pytest

from koskari.extras.setuptools.metadata import (
    KOSKARI_SCRIPTS_GROUP, KoskariScriptsSetupError)
from koskari.extras.setuptools.plugin import (
    KoskariEditableWheel, KoskariInstallScripts, configure_koskari_scripts,
    iter_koskari_script_args, koskari_script_source)


pytestmark = pytest.mark.phase_extras


class FakeDist(object):

    def __init__(
            self,
            entry_points=None,
            install_requires=None,
            zip_safe=True,
            package_data=None,
            packages=None,
            cmdclass=None):

        self.entry_points = entry_points
        if install_requires is None:
            install_requires = ['koskari']
        self.install_requires = install_requires
        self.zip_safe = zip_safe
        self.package_data = package_data or {}
        self.packages = packages
        self.cmdclass = cmdclass or {}


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


class FakeScriptDist(object):

    def __init__(self, entry_points, name='myapp', version='1.0'):
        self.name = name
        self.version = version
        self.entry_points = entry_points


class TestPluginImport(unittest.TestCase):

    def test_plugin_imports_without_koskari_lib(self):
        saved = {}
        for name in list(sys.modules):
            if name in (
                    'koskari.extras.setuptools.plugin',
                    'koskari.launcher',
                    'koskari.lib'):
                saved[name] = sys.modules.pop(name)

        stub = types.ModuleType('koskari.lib')
        sys.modules['koskari.lib'] = stub

        try:
            mod = import_module('koskari.extras.setuptools.metadata')
        finally:
            sys.modules.pop('koskari.lib', None)
            for name, value in saved.items():
                if value is None:
                    sys.modules.pop(name, None)
                else:
                    sys.modules[name] = value

        self.assertTrue(hasattr(mod, 'parse_entry_target'))


class TestConfigureKoskariScripts(unittest.TestCase):

    def test_no_koskari_scripts_is_noop(self):
        dist = FakeDist({'console_scripts': ['koskari = koskari.cli:main']})
        configure_koskari_scripts(dist)
        self.assertEqual(dist.cmdclass, {})

    def test_registers_install_scripts_cmdclass(self):
        dist = FakeDist({
            'koskari_scripts': ['koslife = koslife.cli:main'],
            'console_scripts': ['koskari = koskari.cli:main'],
        }, packages=['koslife'])
        # the fake declares zip_safe=True, which the plugin overrides
        # and announces
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter('always')
            configure_koskari_scripts(dist)
        self.assertTrue(any('zip_safe' in str(w.message) for w in caught))
        self.assertIs(dist.cmdclass['install_scripts'], KoskariInstallScripts)
        self.assertIs(dist.cmdclass['editable_wheel'], KoskariEditableWheel)
        self.assertEqual(
            dist.entry_points['koskari_scripts'],
            ['koslife = koslife.cli:main'])
        self.assertNotIn(
            'koslife',
            [line.split('=')[0].strip()
             for line in dist.entry_points.get('console_scripts', [])])
        self.assertFalse(dist.zip_safe)

    def test_collision_with_console_scripts_raises(self):
        dist = FakeDist({
            'koskari_scripts': ['koslife = koslife.cli:main'],
            'console_scripts': ['koslife = koslife:main'],
        })
        with self.assertRaises(KoskariScriptsSetupError):
            configure_koskari_scripts(dist)

    def test_duplicate_koskari_scripts_raises(self):
        dist = FakeDist({
            'koskari_scripts': [
                'koslife = koslife.cli:main',
                'koslife = koslife.alt:main',
            ],
        })
        with self.assertRaises(KoskariScriptsSetupError):
            configure_koskari_scripts(dist)

    def test_invalid_entry_value_raises(self):
        dist = FakeDist({
            'koskari_scripts': ['koslife = koslife.cli'],
        })
        with self.assertRaises(KoskariScriptsSetupError):
            configure_koskari_scripts(dist)

    def test_setup_cfg_entry_points_configured_after_parse_config_files(self):
        pytest.importorskip('setuptools')
        from setuptools.dist import Distribution

        cfg_text = """
[metadata]
name = demo
version = 0.1.0

[options]
packages =
    demopkg
install_requires =
    koskari

[options.entry_points]
koskari_scripts =
    demotool = demopkg.cli:main
"""

        with tempfile.TemporaryDirectory() as tmp:
            cfg_path = os.path.join(tmp, 'setup.cfg')
            with open(cfg_path, 'w') as fh:
                fh.write(cfg_text)

            old = os.getcwd()
            try:
                os.chdir(tmp)
                dist = Distribution({'script_name': 'setup.py'})
                self.assertIsNone(dist.entry_points)
                dist.parse_config_files()
            finally:
                os.chdir(old)

        self.assertIs(
            dist.cmdclass['install_scripts'],
            KoskariInstallScripts)
        self.assertIs(
            dist.cmdclass['editable_wheel'],
            KoskariEditableWheel)
        self.assertEqual(
            dist.entry_points['koskari_scripts'],
            ['demotool = demopkg.cli:main'])
        self.assertEqual(
            dist.package_data.get('demopkg'),
            ['*.ki', '*.kio'])
        self.assertFalse(dist.zip_safe)

    def test_editable_wheel_installs_koskari_scripts(self):
        cmd = KoskariEditableWheel.__new__(KoskariEditableWheel)
        cmd.distribution = FakeDist({
            'koskari_scripts': ['koslife = koslife.cli:main'],
        })
        calls = []
        cmd.run_command = calls.append
        cmd._run_install('scripts')
        self.assertEqual(calls, ['install_scripts'])
        calls.clear()
        cmd._run_install('data')
        self.assertEqual(calls, [])


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
        dist = FakeScriptDist(FakeEntryPoints({
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
        dist = FakeScriptDist(FakeEntryPoints({
            'console_scripts': [
                FakeEntryPoint('plain', 'plain.cli:main', 'console_scripts'),
            ],
        }))
        self.assertEqual(list(iter_koskari_script_args(dist)), [])


class TestKoskariInstallScripts(unittest.TestCase):

    def test_subclasses_setuptools_install_scripts(self):
        from setuptools.command.install_scripts import install_scripts
        self.assertTrue(issubclass(KoskariInstallScripts, install_scripts))


class TestHookReach(unittest.TestCase):
    """
    The finalize hook runs for every distribution built where koskari is
    installed, so a distribution without koskari_scripts must see no
    setuptools internals loaded and no shared state changed
    """

    def test_importing_the_hook_loads_no_setuptools_internals(self):
        import subprocess
        code = (
            "import sys\n"
            "import koskari.extras.setuptools.plugin\n"
            "loaded = [m for m in sys.modules if m in ("
            "'setuptools._scripts', 'setuptools.command.easy_install',"
            " 'setuptools.command.editable_wheel',"
            " 'koskari.extras.setuptools.commands')]\n"
            "print(loaded)\n")
        out = subprocess.run(
            [sys.executable, "-c", code], capture_output=True, text=True,
            check=True)
        self.assertEqual(out.stdout.strip(), "[]")

    def test_plain_distribution_leaves_the_class_and_dist_alone(self):
        from setuptools.dist import Distribution
        before = Distribution.__dict__['parse_config_files']
        dist = Distribution({'name': 'plain', 'version': '0'})
        configure_koskari_scripts(dist)
        self.assertIs(Distribution.__dict__['parse_config_files'], before)
        self.assertNotIn('install_scripts', dist.cmdclass or {})
        self.assertFalse(getattr(dist, '_koskari_scripts_configured', False))

    def test_declarative_koskari_scripts_seen_after_config_parse(self):
        import os
        import tempfile

        from setuptools.dist import Distribution

        cfg = (
            "[metadata]\n"
            "name = declared\n"
            "version = 0.1\n"
            "[options]\n"
            "packages = declared\n"
            "install_requires = koskari\n"
            "[options.entry_points]\n"
            "koskari_scripts =\n"
            "  hello = declared.cli:main\n")

        cwd = os.getcwd()
        with tempfile.TemporaryDirectory() as tmp:
            os.makedirs(os.path.join(tmp, 'declared'))
            with open(os.path.join(tmp, 'declared', '__init__.py'), 'w'):
                pass
            with open(os.path.join(tmp, 'setup.cfg'), 'w') as f:
                f.write(cfg)
            os.chdir(tmp)
            try:
                dist = Distribution({'name': 'declared'})
                configure_koskari_scripts(dist)
                self.assertNotIn('install_scripts', dist.cmdclass or {})
                dist.parse_config_files()
            finally:
                os.chdir(cwd)

        self.assertIs(dist.cmdclass['install_scripts'], KoskariInstallScripts)
        self.assertTrue(dist._koskari_scripts_configured)

    def test_second_look_is_scoped_to_that_distribution(self):
        from setuptools.dist import Distribution
        class_fn = Distribution.__dict__['parse_config_files']
        first = Distribution({'name': 'first', 'version': '0'})
        configure_koskari_scripts(first)
        # a repeat enables nothing further
        configure_koskari_scripts(first)
        second = Distribution({'name': 'second', 'version': '0'})
        self.assertIs(Distribution.__dict__['parse_config_files'], class_fn)
        self.assertIn('parse_config_files', vars(first))
        self.assertIsNot(vars(first)['parse_config_files'],
                         vars(second).get('parse_config_files'))


# The end.
