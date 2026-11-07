# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
tests.tools.test_pyodide_pytest

The Pyodide test runner resolves a test selection against the
repository root and leaves options and their values alone

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest
from pathlib import Path

import pytest

from tests.pyodide_pytest import resolve_selection


pytestmark = pytest.mark.phase_lib


ROOT = Path(__file__).resolve().parent.parent.parent


class TestResolveSelection(unittest.TestCase):

    def test_empty_selection_is_the_tests_directory(self):
        self.assertEqual(
            [str(ROOT / 'tests')], resolve_selection(ROOT, []))


    def test_relative_file_and_directory_take_the_root(self):
        got = resolve_selection(
            ROOT, ['tests/lib/test_pair.py', 'tests/compile'])
        self.assertEqual(
            [str(ROOT / 'tests/lib/test_pair.py'),
             str(ROOT / 'tests/compile')],
            got)


    def test_node_id_keeps_its_suffix(self):
        got = resolve_selection(
            ROOT, ['tests/lib/test_pair.py::TestPair::test_car'])
        self.assertEqual(
            [str(ROOT / 'tests/lib/test_pair.py::TestPair::test_car')],
            got)


    def test_options_and_their_values_pass_through(self):
        args = ['-v', '-k', 'nil', '--tb=short', '-m', 'phase_lib']
        self.assertEqual(args, resolve_selection(ROOT, args))


    def test_absolute_path_passes_through(self):
        absolute = str(ROOT / 'tests' / 'lib')
        self.assertEqual([absolute], resolve_selection(ROOT, [absolute]))


    def test_unknown_relative_name_passes_through(self):
        self.assertEqual(
            ['no/such/path.py'],
            resolve_selection(ROOT, ['no/such/path.py']))


# The end.
