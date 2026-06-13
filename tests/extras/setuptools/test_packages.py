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
tests.extras.setuptools.test_packages

Unit tests for koskari package augmentation on declared packages.
"""


import os
import shutil
import tempfile
import unittest
import warnings

from koskari.extras.setuptools.packages import (
    augment_koskari_packages,
    packages_for_module_name,
    packages_from_koskari_scripts,
    scan_koskari_packages,
    warn_undeclared_script_packages,
)

import pytest

pytestmark = pytest.mark.phase_extras


class FakeDist(object):

    def __init__(
            self,
            entry_points=None,
            packages=None,
            package_data=None,
            script_name=None,
            package_dir=None,
            zip_safe=True):

        self.entry_points = entry_points
        self.packages = packages
        self.package_data = package_data or {}
        self.script_name = script_name
        self.package_dir = package_dir
        self.zip_safe = zip_safe


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

    def _write_ki(self, relpath):
        path = os.path.join(self._tmpdir, relpath)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(';; test\n')

    def test_scan_is_diagnostic_only(self):
        self._write_ki('koslife/cli.ki')
        dist = FakeDist(script_name=self._setup_path)
        self.assertEqual(
            sorted(scan_koskari_packages(dist)),
            ['koslife'])

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
        augment_koskari_packages(dist)
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
