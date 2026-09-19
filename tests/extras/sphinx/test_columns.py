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
tests.extras.sphinx.test_columns

The dialogue and pair two-column directives
"""

import unittest
from unittest.mock import MagicMock

import pytest


pytest.importorskip('sphinx')

from koskari.extras.sphinx.columns import (
    CSS_FILE, DialogueDirective, PairDirective, add_static_path,
    register_columns, split_columns, static_dir)


pytestmark = pytest.mark.phase_extras


class TestSplitColumns(unittest.TestCase):

    def test_splits_at_separator(self):
        left, right = split_columns(['What is x?', '+++', 'A name.', ''])
        self.assertEqual(left, ['What is x?'])
        self.assertEqual(right, ['A name.', ''])


    def test_separator_may_carry_whitespace(self):
        left, right = split_columns(['q', '  +++  ', 'a'])
        self.assertEqual(left, ['q'])
        self.assertEqual(right, ['a'])


    def test_missing_separator_is_an_error(self):
        with self.assertRaises(ValueError):
            split_columns(['q', 'a'])


    def test_repeated_separator_is_an_error(self):
        with self.assertRaises(ValueError):
            split_columns(['q', '+++', 'a', '+++', 'b'])


class TestDirectiveShape(unittest.TestCase):

    def test_dialogue_columns_are_unlabelled(self):
        directive = DialogueDirective.__new__(DialogueDirective)
        directive.options = {}
        self.assertEqual(directive.labels(), (None, None))


    def test_pair_columns_default_to_python_and_koskari(self):
        directive = PairDirective.__new__(PairDirective)
        directive.options = {}
        self.assertEqual(directive.labels(), ('Python', 'Koskari'))


    def test_pair_labels_come_from_options(self):
        directive = PairDirective.__new__(PairDirective)
        directive.options = {'left': 'Scheme', 'right': 'Koskari'}
        self.assertEqual(directive.labels(), ('Scheme', 'Koskari'))


class TestRegistration(unittest.TestCase):

    def test_registers_directives_and_stylesheet(self):
        app = MagicMock()
        register_columns(app)

        names = [call.args[0] for call in app.add_directive.call_args_list]
        self.assertEqual(names, ['dialogue', 'pair'])
        app.add_css_file.assert_called_once_with(CSS_FILE)
        app.connect.assert_called_once()
        self.assertEqual(app.connect.call_args.args[0], 'config-inited')


    def test_stylesheet_ships_with_the_package(self):
        self.assertTrue((static_dir() / CSS_FILE).is_file())


    def test_static_path_added_once(self):
        config = MagicMock()
        config.html_static_path = []
        add_static_path(None, config)
        add_static_path(None, config)
        self.assertEqual(config.html_static_path, [str(static_dir())])


# The end.
