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
tests.extras.setuptools.test_validate

Unit tests for koskari_scripts metadata validation helpers.
"""


import unittest
import warnings

from koskari.extras.setuptools.errors import KoskariScriptsSetupError
from koskari.extras.setuptools.validate import (
    parse_entry_point_line,
    validate_entry_point_value,
    validate_koskari_scripts,
    warn_koskari_scripts_metadata,
)

import pytest

pytestmark = pytest.mark.phase_extras


class FakeDist(object):

    def __init__(
            self,
            entry_points=None,
            install_requires=None,
            zip_safe=False,
            package_data=None):

        self.entry_points = entry_points
        if install_requires is None:
            install_requires = ['koskari']
        self.install_requires = install_requires
        self.zip_safe = zip_safe
        self.package_data = package_data or {}


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

    def test_validate_entry_point_value_rejects_bare_module(self):
        with self.assertRaises(KoskariScriptsSetupError):
            validate_entry_point_value('koslife.cli')

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

    def test_warn_zip_safe_with_ki_package_data(self):
        dist = FakeDist({
            'koskari_scripts': ['koslife = koslife.cli:main'],
        }, zip_safe=True, package_data={'koslife': ['*.ki']})
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter('always')
            warn_koskari_scripts_metadata(dist)
        self.assertTrue(caught)
        self.assertIn('zip_safe', str(caught[0].message))


# The end.
