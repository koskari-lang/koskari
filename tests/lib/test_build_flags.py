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
Unit tests for koskari.lib.build_flags

build_flags is a read-only mapping from the name of each build knob that
was defined when the extension compiled to its integer value. Tests that
assert a property one knob removes gate on it.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest
from collections.abc import Mapping

import pytest

from koskari.lib import build_flags


pytestmark = pytest.mark.phase_lib


class TestBuildFlags(unittest.TestCase):

    def test_is_read_only_mapping(self):
        self.assertIsInstance(build_flags, Mapping)
        with self.assertRaises(TypeError):
            build_flags['KOS_NOFREELIST'] = 1

    def test_names_and_values(self):
        for name, value in build_flags.items():
            self.assertIsInstance(name, str)
            self.assertTrue(name.isupper(), name)
            self.assertIsInstance(value, int)

    def test_musttail_always_present(self):
        # the evaluator refuses to build without it, so every extension
        # that imports reports it on
        self.assertEqual(build_flags.get('KOS_HAVE_MUSTTAIL'), 1)

    def test_no_protect_always_present(self):
        self.assertIn(build_flags['KOS_NO_PROTECT'], (0, 1))

    def test_absent_knob_is_absent(self):
        self.assertIsNone(build_flags.get('KOS_NO_SUCH_KNOB'))


# The end.
