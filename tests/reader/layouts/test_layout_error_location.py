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
tests.reader.layouts.test_layout_error_location

A LayoutError raised by a layout handler names the file, the line and
the source text of the block it was reading, as a reader syntax error
does: from the handler's own position when it has one, and from the
block's start when a raw layout has none. A decorator line or a
decorated line that the decorator read folds names its own line.
"""

from textwrap import dedent
from unittest import TestCase

import pytest

import koskari.compile.specials as _koskari_compile_specials  # noqa: F401
import koskari.reader.layouts as _koskari_reader_layouts  # noqa: F401
from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.exceptions import LayoutError
from koskari.reader import (
    READ_EOF, IndentAwareStream, LayoutReader, source_str)


pytestmark = pytest.mark.phase_infra


FILENAME = "/nonexistent/layout_error_location.ki"


def read_all(src, **modes):
    reader = LayoutReader(dict(_BUILTIN_REGISTRY))
    reader.set_layout_modes(**modes)
    stream = IndentAwareStream.from_source(
        source_str(dedent(src), FILENAME))
    out = []
    while True:
        form = reader.read(stream)
        if form is READ_EOF:
            break
        out.append(form)
    return out


class TestLayoutErrorLocation(TestCase):

    def read_error(self, src):
        with self.assertRaises(LayoutError) as caught:
            read_all(src)
        return caught.exception


    def test_handler_position(self):
        err = self.read_error("""
        (print 1)
        try:
          (print 2)
        (print 3)
        """)
        self.assertEqual(FILENAME, err.filename)
        self.assertEqual(3, err.lineno)
        self.assertEqual("try:\n", err.text)
        self.assertIn("line 3", str(err))


    def test_raw_layout_block_start(self):
        err = self.read_error("""
        (print 1)
        switch:
        """)
        self.assertEqual(FILENAME, err.filename)
        self.assertEqual(3, err.lineno)
        self.assertIn("line 3", str(err))


    def test_decorator_line(self):
        """
        Several forms on a later decorator line name that line
        """

        with self.assertRaises(LayoutError) as caught:
            read_all("""
            @f
            @g 1
            print
            """, layout_implicit_calls=False, layout_decorators=True)
        err = caught.exception
        self.assertEqual(FILENAME, err.filename)
        self.assertEqual(3, err.lineno)
        self.assertEqual("@g 1\n", err.text)


    def test_decorated_line(self):
        """
        Several forms on the decorated line name that line
        """

        with self.assertRaises(LayoutError) as caught:
            read_all("""
            @f
            print 1
            """, layout_implicit_calls=False, layout_decorators=True)
        err = caught.exception
        self.assertEqual(FILENAME, err.filename)
        self.assertEqual(3, err.lineno)
        self.assertEqual("print 1\n", err.text)


# The end.
