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
tests.reader.layouts.test_layout_prefix_eol

A prefix reader macro at the end of a block body line has no operand
on that line, and the next line is outside the block: the read ends
at the dedent and the prefix reports the EOF it opened, in place of
taking the next line's first token as its operand and dropping the
line that held it. At top level, where no block bounds the read, a
prefix takes the next form as an s-expression reader does.
"""

from textwrap import dedent
from unittest import TestCase

import pytest

import koskari.compile.specials as _koskari_compile_specials  # noqa: F401
import koskari.reader.layouts as _koskari_reader_layouts  # noqa: F401
from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.exceptions import ReaderEOF
from koskari.lib import symbol
from koskari.reader import (
    READ_EOF, IndentAwareStream, LayoutReader, source_str)


pytestmark = pytest.mark.phase_infra


def read_all(src):
    reader = LayoutReader(dict(_BUILTIN_REGISTRY))
    stream = IndentAwareStream.from_source(
        source_str(dedent(src), '<unittest>'))
    out = []
    while True:
        form = reader.read(stream)
        if form is READ_EOF:
            break
        out.append(form)
    return out


class TestPrefixAtEndOfBlockLine(TestCase):

    def assert_eof_at(self, src, line):
        with self.assertRaises(ReaderEOF) as caught:
            read_all(src)
        self.assertEqual(line, caught.exception.lineno)


    def test_quote_at_end_of_body_line(self):
        self.assert_eof_at("""
        define f []:
          (print "one")
          print "two" '
        (print "three")
        """, 4)


    def test_quasiquote_and_quote_syntax(self):
        self.assert_eof_at("""
        define f []:
          print "two" `
        (print "three")
        """, 3)
        self.assert_eof_at("""
        define f []:
          print "two" #'
        (print "three")
        """, 3)


    def test_top_level_prefix_takes_the_next_form(self):
        forms = read_all("""
        '
        foo
        """)
        self.assertEqual(1, len(forms))
        self.assertEqual(symbol('quote'), forms[0].car)
        self.assertEqual(symbol('foo'), forms[0].cdr.car)


# The end.
