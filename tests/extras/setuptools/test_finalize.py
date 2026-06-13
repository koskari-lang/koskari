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
tests.extras.setuptools.test_finalize

Unit tests for koskari_scripts console_scripts mirroring.
"""


import unittest

from koskari.extras.setuptools.errors import KoskariScriptsSetupError
from koskari.extras.setuptools.finalize import mirror_koskari_scripts
from koskari.launcher import DISPATCH_TARGET

import pytest

pytestmark = pytest.mark.phase_extras


class FakeDist(object):

    def __init__(
            self,
            entry_points=None,
            install_requires=None,
            zip_safe=True,
            package_data=None,
            packages=None):

        self.entry_points = entry_points
        if install_requires is None:
            install_requires = ['koskari']
        self.install_requires = install_requires
        self.zip_safe = zip_safe
        self.package_data = package_data or {}
        self.packages = packages


class TestMirrorKoskariScripts(unittest.TestCase):

    def test_no_koskari_scripts_is_noop(self):
        dist = FakeDist({'console_scripts': ['koskari = koskari.cli:main']})
        mirror_koskari_scripts(dist)
        self.assertEqual(
            dist.entry_points['console_scripts'],
            ['koskari = koskari.cli:main'])

    def test_mirror_appends_console_scripts(self):
        dist = FakeDist({
            'koskari_scripts': ['koslife = koslife.cli:main'],
            'console_scripts': ['koskari = koskari.cli:main'],
        }, packages=['koslife'])
        mirror_koskari_scripts(dist)
        self.assertIn(
            'koslife = %s' % (DISPATCH_TARGET,),
            dist.entry_points['console_scripts'])
        self.assertEqual(
            dist.entry_points['koskari_scripts'],
            ['koslife = koslife.cli:main'])
        self.assertFalse(dist.zip_safe)

    def test_collision_with_console_scripts_raises(self):
        dist = FakeDist({
            'koskari_scripts': ['koslife = koslife.cli:main'],
            'console_scripts': ['koslife = koslife:main'],
        })
        with self.assertRaises(KoskariScriptsSetupError):
            mirror_koskari_scripts(dist)

    def test_duplicate_koskari_scripts_raises(self):
        dist = FakeDist({
            'koskari_scripts': [
                'koslife = koslife.cli:main',
                'koslife = koslife.alt:main',
            ],
        })
        with self.assertRaises(KoskariScriptsSetupError):
            mirror_koskari_scripts(dist)

    def test_invalid_entry_value_raises(self):
        dist = FakeDist({
            'koskari_scripts': ['koslife = koslife.cli'],
        })
        with self.assertRaises(KoskariScriptsSetupError):
            mirror_koskari_scripts(dist)


# The end.
