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
tests.reader.layouts.test_layout_keyword_head

Under the default block layout a line or block that opens with a keyword
is a call argument name and its value. A keyword line splices as two
arguments, a keyword header reads the rest of the header as the value
block's head, and a bare keyword header reads a begin block.
"""

from unittest import TestCase

import pytest

from koskari.compile.helpers import pycons
from koskari.exceptions import LayoutError
from koskari.lib import keyword, symbol

from .. import parse_layout


pytestmark = pytest.mark.phase_infra


class TestKeywordLines(TestCase):

    def test_single_value_splices(self):
        src = """
        print:
          "hello"
          file: stderr
        """
        expected = pycons(
            (symbol('print'), 'hello', keyword('file:'), symbol('stderr')))
        self.assertEqual(parse_layout(src), expected)


    def test_several_values_fold_to_a_call(self):
        src = """
        print:
          sep: 1 2
        """
        expected = pycons(
            (symbol('print'), keyword('sep:'), (1, 2)))
        self.assertEqual(parse_layout(src), expected)


    def test_named_layout_body_keeps_the_pair(self):
        src = """
        if x:
          file: stderr
        """
        expected = pycons(
            (symbol('cond'),
             (symbol('x'), (keyword('file:'), symbol('stderr')))))
        self.assertEqual(parse_layout(src), expected)


class TestEmptyBody(TestCase):

    def test_empty_body_adds_no_argument(self):
        src = """
        print:
        + 1 2
        """
        expected = pycons((symbol('print'),))
        self.assertEqual(parse_layout(src), expected)


class TestKeywordLineVersusPair(TestCase):

    def test_written_pair_is_not_a_keyword_line(self):
        src = """
        cond:
          [(== v 0) "a0"]
          [else: "meh"]
        """
        expected = pycons(
            (symbol('cond'),
             ((symbol('=='), symbol('v'), 0), 'a0'),
             (keyword('else:'), 'meh')))
        self.assertEqual(parse_layout(src), expected)


    def test_paren_pair_is_not_a_keyword_line_either(self):
        src = """
        foo:
          (else: x)
          else: x
        """
        expected = pycons(
            (symbol('foo'),
             (keyword('else:'), symbol('x')),
             keyword('else:'), symbol('x')))
        self.assertEqual(parse_layout(src), expected)


class TestKeywordBlocks(TestCase):

    def test_value_block_uses_its_own_head(self):
        src = """
        print:
          file: begin:
            (log "choosing")
            stderr
          "hello"
        """
        expected = pycons(
            (symbol('print'),
             keyword('file:'),
             (symbol('begin'), (symbol('log'), 'choosing'), symbol('stderr')),
             'hello'))
        self.assertEqual(parse_layout(src), expected)


    def test_value_block_head_resolves_its_layout(self):
        src = """
        print:
          file: let [[x 1]]:
            x
        """
        inline = """
        print:
          file: (let [[x 1]] x)
        """
        self.assertEqual(parse_layout(src), parse_layout(inline))


    def test_bare_keyword_header_is_a_begin_block(self):
        src = """
        print:
          file::
            log "choosing"
            stderr
          "hello"
        """
        expected = pycons(
            (symbol('print'),
             keyword('file:'),
             (symbol('begin'), (symbol('log'), 'choosing'), symbol('stderr')),
             'hello'))
        self.assertEqual(parse_layout(src), expected)


    def test_bare_keyword_header_equals_explicit_begin(self):
        bare = """
        print:
          file::
            let [[x 1]]:
              x
        """
        explicit = """
        print:
          file: begin:
            let [[x 1]]:
              x
        """
        self.assertEqual(parse_layout(bare), parse_layout(explicit))


    def test_peer_block_nests_under_bare_keyword(self):
        src = """
        print:
          file::
            if redirect?:
              stdout
            else:
              stderr
          "hello"
        """
        expected = pycons(
            (symbol('print'),
             keyword('file:'),
             (symbol('begin'),
              (symbol('cond'),
               (symbol('redirect?'), symbol('stdout')),
               (keyword('else:'), symbol('stderr')))),
             'hello'))
        self.assertEqual(parse_layout(src), expected)


    def test_peer_head_after_keyword_is_refused(self):
        src = """
        print:
          file: if redirect?:
            stdout
          else:
            stderr
        """
        with self.assertRaises(LayoutError):
            parse_layout(src)


    def test_quoted_block_keeps_keyword_head_raw(self):
        src = """
        'print:
          file::
            x
        """
        expected = pycons(
            (symbol('quote'),
             (keyword('#block'), (symbol('print'),),
              (keyword('#block'), (keyword('file:'),),
               (keyword('#line'), symbol('x'))))))
        self.assertEqual(parse_layout(src), expected)


# The end.


class TestMarkerNamedKeywordLines(TestCase):
    """
    The block and splice markers of the layout IR are the keywords
    #block: and #splice:, so a keyword argument named block: or splice:
    on a body line is an ordinary keyword line
    """

    def test_block_keyword_line_is_an_argument(self):
        src = """
        f 1:
          block: False
        """
        expected = pycons((symbol('f'), 1, keyword('block:'), False))
        self.assertEqual(parse_layout(src), expected)


    def test_splice_keyword_line_is_an_argument(self):
        src = """
        f 1:
          splice: True
        """
        expected = pycons((symbol('f'), 1, keyword('splice:'), True))
        self.assertEqual(parse_layout(src), expected)


    def test_quoted_list_headed_by_block_keyword_is_data(self):
        src = """
        list:
          '(block: (a) (b c))
        """
        quoted = (symbol('quote'),
                  (keyword('block:'), (symbol('a'),), (symbol('b'), symbol('c'))))
        expected = pycons((symbol('list'), quoted))
        self.assertEqual(parse_layout(src), expected)
