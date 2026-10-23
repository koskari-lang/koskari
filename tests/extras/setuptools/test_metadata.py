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
tests.extras.setuptools.test_metadata

Unit tests for koskari_scripts metadata helpers.
"""


import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import warnings

import pytest

from koskari.extras.setuptools.metadata import (
    KOSKARI_SCRIPTS_GROUP, EntryTargetError, KoskariScriptsSetupError,
    augment_koskari_packages, packages_for_module_name,
    packages_from_koskari_scripts, parse_entry_point_line, parse_entry_target,
    validate_entry_point_value, validate_koskari_scripts,
    warn_koskari_scripts_metadata, warn_undeclared_script_packages)


pytestmark = pytest.mark.phase_extras


class TestMetadataImport(unittest.TestCase):

    def test_metadata_imports_without_setuptools(self):
        import subprocess

        script = (
            'import importlib, sys\n'
            'importlib.import_module("koskari.extras.setuptools.metadata")\n'
            'assert "setuptools" not in sys.modules\n')

        result = subprocess.run(
            [sys.executable, '-c', script],
            capture_output=True,
            text=True)

        self.assertEqual(
            result.returncode,
            0,
            result.stderr or result.stdout)


class FakeDist(object):

    def __init__(
            self,
            entry_points=None,
            install_requires=None,
            zip_safe=False,
            package_data=None,
            packages=None,
            script_name=None,
            package_dir=None):

        self.entry_points = entry_points
        if install_requires is None:
            install_requires = ['koskari']
        self.install_requires = install_requires
        self.zip_safe = zip_safe
        self.package_data = package_data or {}
        self.packages = packages
        self.script_name = script_name
        self.package_dir = package_dir


class TestEntryTarget(unittest.TestCase):

    def test_koskari_scripts_group_constant(self):
        self.assertEqual(KOSKARI_SCRIPTS_GROUP, 'koskari_scripts')

    def test_parse_entry_target(self):
        self.assertEqual(
            parse_entry_target('koslife.cli:main'),
            ('koslife.cli', 'main'))

    def test_parse_entry_target_bare_module_is_its_entry_point(self):
        self.assertEqual(
            parse_entry_target('koslife.cli'),
            ('koslife.cli', None))

    def test_parse_entry_target_rejects_empty_callable(self):
        with self.assertRaises(EntryTargetError):
            parse_entry_target('koslife.cli:')

    def test_parse_entry_target_rejects_empty_module(self):
        with self.assertRaises(EntryTargetError):
            parse_entry_target(':main')


class TestValidateHelpers(unittest.TestCase):

    def test_parse_entry_point_line(self):
        self.assertEqual(
            parse_entry_point_line('koslife = koslife.cli:main'),
            ('koslife', 'koslife.cli:main'))

    def test_parse_entry_point_line_rejects_empty(self):
        with self.assertRaises(KoskariScriptsSetupError):
            parse_entry_point_line('broken')

    def test_validate_entry_point_value_accepts_callable_form(self):
        validate_entry_point_value('koslife.cli:main')

    def test_validate_entry_point_value_accepts_bare_module(self):
        validate_entry_point_value('koslife.cli')

    def test_validate_entry_point_value_rejects_empty_callable(self):
        with self.assertRaises(KoskariScriptsSetupError):
            validate_entry_point_value('koslife.cli:')

    def test_validate_koskari_scripts_noop_when_absent(self):
        validate_koskari_scripts(FakeDist({'console_scripts': []}))

    def test_warn_missing_install_requires(self):
        dist = FakeDist({
            'koskari_scripts': ['koslife = koslife.cli:main'],
        }, install_requires=[])
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter('always')
            warn_koskari_scripts_metadata(dist)
        self.assertTrue(caught)
        self.assertIn('install_requires', str(caught[0].message))

    def test_dependency_check_reads_the_requirement_name(self):
        # a specifier, an extra, or a differently cased name still names
        # koskari; an unrelated requirement does not
        for reqs in (['koskari>=0.1'], ['koskari[extras] ; python_version'
                                        ' >= "3.9"'], ['Koskari'],
                     ['requests', 'KOSKARI == 1.0']):
            dist = FakeDist({
                'koskari_scripts': ['koslife = koslife.cli:main'],
            }, install_requires=reqs)
            with warnings.catch_warnings(record=True) as caught:
                warnings.simplefilter('always')
                warn_koskari_scripts_metadata(dist)
            self.assertEqual([], caught, reqs)

        dist = FakeDist({
            'koskari_scripts': ['koslife = koslife.cli:main'],
        }, install_requires=['requests'])
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter('always')
            warn_koskari_scripts_metadata(dist)
        self.assertEqual(1, len(caught))
        self.assertIn('install_requires', str(caught[0].message))

    def test_zip_safe_override_is_announced_where_it_happens(self):
        # the plugin sets zip_safe False for declared packages; an author
        # who asked for True hears about the override, one who did not
        # hears nothing about zip_safe
        dist = FakeDist({
            'koskari_scripts': ['koslife = koslife.cli:main'],
        }, zip_safe=True, packages=['koslife'])
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter('always')
            augment_koskari_packages(dist)
        zip_warnings = [w for w in caught if 'zip_safe' in str(w.message)]
        self.assertEqual(1, len(zip_warnings))
        self.assertFalse(dist.zip_safe)

        dist = FakeDist({
            'koskari_scripts': ['koslife = koslife.cli:main'],
        }, zip_safe=False, packages=['koslife'])
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter('always')
            augment_koskari_packages(dist)
            warn_koskari_scripts_metadata(dist)
        self.assertEqual(
            [], [w for w in caught if 'zip_safe' in str(w.message)])


class TestPackagesForModuleName(unittest.TestCase):

    def test_nested_module_yields_parent_packages(self):
        self.assertEqual(
            packages_for_module_name('koslife.cli'),
            ['koslife'])

    def test_deep_module_yields_all_parents(self):
        self.assertEqual(
            packages_for_module_name('foo.bar.baz'),
            ['foo', 'foo.bar'])


class TestAugmentKoskariPackages(unittest.TestCase):

    def setUp(self):
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_pkg_')
        self._setup_path = os.path.join(self._tmpdir, 'setup.py')
        with open(self._setup_path, 'w', encoding='utf-8') as fh:
            fh.write('# placeholder setup.py\n')

    def tearDown(self):
        shutil.rmtree(self._tmpdir, ignore_errors=True)

    def test_packages_from_scripts(self):
        dist = FakeDist(entry_points={
            'koskari_scripts': ['koslife = koslife.cli:main'],
        })
        self.assertEqual(
            sorted(packages_from_koskari_scripts(dist.entry_points)),
            ['koslife'])

    def test_augment_requires_explicit_packages(self):
        dist = FakeDist(
            entry_points={
                'koskari_scripts': ['koslife = koslife.cli:main'],
            },
            script_name=self._setup_path,
            zip_safe=True)
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter('always')
            augment_koskari_packages(dist)
        self.assertIsNone(dist.packages)
        self.assertTrue(dist.zip_safe)
        self.assertTrue(any(
            'packages=' in str(item.message)
            for item in caught))

    def test_augment_sets_package_data_and_zip_safe(self):
        dist = FakeDist(
            entry_points={
                'koskari_scripts': ['koslife = koslife.cli:main'],
            },
            packages=['koslife'],
            zip_safe=True)
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter('always')
            augment_koskari_packages(dist)
        self.assertTrue(any('zip_safe' in str(w.message) for w in caught))
        self.assertEqual(dist.packages, ['koslife'])
        self.assertEqual(
            dist.package_data,
            {'koslife': ['*.ki', '*.kio']})
        self.assertFalse(dist.zip_safe)

    def test_augment_merges_existing_package_data(self):
        dist = FakeDist(
            entry_points={
                'koskari_scripts': ['koslife = koslife.cli:main'],
            },
            packages=['koslife', 'other'],
            package_data={'koslife': ['README.md'], 'other': ['*.txt']})
        augment_koskari_packages(dist)
        self.assertEqual(
            dist.package_data['koslife'],
            ['README.md', '*.ki', '*.kio'])
        self.assertEqual(
            dist.package_data['other'],
            ['*.txt', '*.ki', '*.kio'])

    def test_warn_undeclared_script_packages(self):
        dist = FakeDist(
            entry_points={
                'koskari_scripts': ['koslife = koslife.cli:main'],
            },
            packages=['other'])
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter('always')
            warn_undeclared_script_packages(dist)
        self.assertTrue(caught)
        self.assertIn('koslife', str(caught[0].message))


# The end.
