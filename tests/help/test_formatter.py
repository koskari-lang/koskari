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
Tests for HelpFormatter rendering

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import re
import unittest

import pytest


pytestmark = pytest.mark.phase_basics

HelpFormatter = None


def setUpModule():
    """
    Import koskari.help at execution time, not collection

    koskari.help is written in Koskari; loading it needs the import
    hook and the full bootstrap, which earlier phases should not pull
    in during collection.
    """

    global HelpFormatter

    from koskari.importer import install
    install()

    from koskari.help import HelpFormatter


def _plain_help(text):
    return re.sub(r'.\x08', '', text)


class TestHelpFormatter(unittest.TestCase):


    def test_empty_formatter(self):
        fmt = HelpFormatter()
        self.assertTrue(fmt.is_empty())
        self.assertEqual(fmt.render(), '')


    def test_section_heading_and_paragraph(self):
        root = HelpFormatter()
        sec = root.section('Topic')
        sec.paragraph('Body text')
        text = _plain_help(root.render())
        self.assertIn('Topic', text)
        self.assertIn('Body text', text)
        self.assertIn('\n    Body text', text)


    def test_subsection_and_items(self):
        root = HelpFormatter()
        sec = root.section('Outer')
        sub = sec.section('Inner')
        sub.item('first arm')
        sub.item('second arm')
        text = _plain_help(root.render())
        self.assertIn('Outer', text)
        self.assertIn('Inner', text)
        self.assertIn('first arm', text)
        self.assertIn('second arm', text)


# The end.
