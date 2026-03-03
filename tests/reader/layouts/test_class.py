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
Tests for koskari.reader.layouts.classes layout_class.

class block syntax with optional superclass list.
"""


from unittest import TestCase

from koskari.types import symbol
from koskari.compile.helpers import pycons
from .. import parse_layout


class TestLayoutClass(TestCase):

    def test_class_omit_supers(self):
        src = """
        class C:
          x
        """
        expected = pycons(
            (symbol('compile/class'),
             symbol('C'),
             (),
             (symbol('compile/begin'), symbol('x'))))
        result = parse_layout(src)
        self.assertEqual(result, expected)


    def test_class_with_supers(self):
        src = """
        class C (Base):
          x
        """
        expected = pycons(
            (symbol('compile/class'),
             symbol('C'),
             (symbol('Base'),),
             (symbol('compile/begin'), symbol('x'))))
        result = parse_layout(src)
        self.assertEqual(result, expected)


    def test_class_empty_supers(self):
        src = """
        class C ():
          x
        """
        expected = pycons(
            (symbol('compile/class'),
             symbol('C'),
             (),
             (symbol('compile/begin'), symbol('x'))))
        result = parse_layout(src)
        self.assertEqual(result, expected)


# The end.
