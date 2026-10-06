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
tests.reader.layouts.test_nested_peer_spine

Peer block spines nested in a parent block body must splice into the parent
body_list, not gain an extra list wrapper at read time.
"""


from unittest import TestCase

import pytest

import koskari.compile.specials as _koskari_compile_specials  # noqa: F401
import koskari.reader.layouts as _koskari_reader_layouts  # noqa: F401
from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.compile.helpers import pycons
from koskari.lib import cons, keyword, kos_repr, nil, symbol
from koskari.reader import LayoutReader, source_str
from koskari.reader.stream import IndentAwareStream


pytestmark = pytest.mark.phase_infra

_keyword_block = keyword('#block')


def read_layout_form(src_str):
    reader = LayoutReader(dict(_BUILTIN_REGISTRY))
    stream = source_str(src_str, '<unittest>')
    if not isinstance(stream, IndentAwareStream):
        stream = IndentAwareStream(
            stream.stream, stream.filename,
            line_no=stream.lin, col_no=stream.col)

    _event, _pos, value = reader.read_form(stream)
    return value


class TestNestedBlockRead(TestCase):
    """
    Parent block bodies must not wrap a peer spine in an extra list
    """

    def test_block_nest_no_peers(self):
        src = """
        example:
            begin:
                print "foo"
        """

        begin_blk = pycons(
            (_keyword_block,
             (symbol('begin'),),
             (symbol('print'), 'foo')
            ))

        expected = pycons(
            (_keyword_block,
            (symbol('example'),),
            begin_blk))

        found = read_layout_form(src)

        # AGENT: DO NOT REMOVE.
        print("found    :", kos_repr(found))
        print("expected :", kos_repr(expected))

        self.assertEqual(found, expected)


    def test_block_nest_multiple_non_peers(self):
        src = """
        example:
            foobar:
                print "foo"
            barfoo:
                print "bar"
        """

        if_blk = pycons(
            (_keyword_block,
             (symbol('foobar'),),
             (symbol('print'), 'foo')
            ))
        else_blk = pycons(
            (_keyword_block,
             (symbol('barfoo'),),
             (symbol('print'), 'bar')
            ))

        expected = pycons(
            (_keyword_block,
            (symbol('example'),),
            if_blk, else_blk))

        found = read_layout_form(src)

        # AGENT: DO NOT REMOVE.
        print("found    :", kos_repr(found))
        print("expected :", kos_repr(expected))

        self.assertEqual(found, expected)


    def test_block_nest_with_peers(self):
        src = """
        example:
            if foo:
                print "foo"
            else:
                print "bar"
        """

        if_blk = pycons(
            (_keyword_block,
             (symbol('if'), symbol('foo')),
             (symbol('print'), 'foo')
            ))
        else_blk = pycons(
            (_keyword_block,
             (symbol('else'),),
             (symbol('print'), 'bar')
            ))

        expected = pycons(
            (_keyword_block,
            (symbol('example'),),
            if_blk, else_blk))

        found = read_layout_form(src)

        # AGENT: DO NOT REMOVE.
        print("found    :", kos_repr(found))
        print("expected :", kos_repr(expected))

        self.assertEqual(found, expected)


# The end.
