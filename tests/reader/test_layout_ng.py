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
tests.reader.test_layout_ng

Pass 1: strict layout_ng header semantics TDD.
"""

from unittest import IsolatedAsyncioTestCase

from koskari.exceptions import ReaderSyntaxError
from koskari.compile.helpers import pycons
from koskari.reader import source_str
from koskari.reader.layouts import LAYOUTS
from koskari.reader.layout_ng import LayoutNgReader
from koskari.types import cons, keyword, nil, symbol

import pytest

pytestmark = pytest.mark.phase_infra


def parse_layout_ng(src_str, **defns):
    builtins = {str(name): value for name, value in LAYOUTS.items()}
    builtins.update(defns)
    reader = LayoutNgReader(builtins)
    stream = source_str(src_str, '<unittest>')
    return reader.read(stream)


class TestLayoutNgStrictHeaders(IsolatedAsyncioTestCase):

    async def test_missing_trailing_colon_is_error(self):
        src = """
        define foo []
          (print 1)
          (do-something-dangerous)
        """

        with self.assertRaises(ReaderSyntaxError):
            parse_layout_ng(src)

    async def test_begin_colon_is_header(self):
        src = """
        begin:
          x
          y
        """

        expected = pycons(
            (symbol("compile/begin"),
             symbol("x"),
             symbol("y"))
        )

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_begin_without_colon_is_symbol_only(self):
        src = """
        begin
          x
        """

        result = parse_layout_ng(src)
        self.assertEqual(result, symbol('begin'))

    async def test_head_comment_disappears_from_commit_decision(self):
        src = """
        begin ;; comment
          x
        """

        result = parse_layout_ng(src)
        self.assertEqual(result, symbol('begin'))

    async def test_committing_to_header_requires_structural_colon(self):
        src = """
        define [x]
          y
        """

        with self.assertRaises(ReaderSyntaxError):
            parse_layout_ng(src)


class TestLayoutNgReaderParity(IsolatedAsyncioTestCase):

    async def test_simple_block(self):
        src = """
        foo x:
          y
        """

        expected = pycons(
            (symbol('foo'),
             symbol('x'),
             (symbol('compile/begin'), symbol('y')),
             )
        )

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_header_colon_terminator_with_comment_after(self):
        src = """
        unknown x:  ;; comment
          y
        """

        expected = pycons(
            (symbol('unknown'),
             symbol('x'),
             (symbol('compile/begin'), symbol('y')),
             )
        )

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_keyword_atoms_at_depth0_in_header_args(self):
        src = """
        unknown as: x:
          y
        """

        expected = pycons(
            (symbol('unknown'),
             keyword('as'),
             symbol('x'),
             (symbol('compile/begin'), symbol('y')),
             )
        )

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_keyword_colon_not_structural_when_followed_by_token(self):
        src = """
        unknown x: y:
          z
        """

        expected = pycons(
            (symbol('unknown'),
             keyword('x'),
             symbol('y'),
             (symbol('compile/begin'), symbol('z')),
             )
        )

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_trailing_keyword(self):
        src = """
        unknown x::
          z
        """

        expected = pycons(
            (symbol('unknown'),
             keyword('x'),
             (symbol('compile/begin'), symbol('z')),
             )
        )

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_crlf_header_terminator(self):
        src = "unknown x:\r\n  y\n"

        expected = pycons(
            (symbol('unknown'),
             symbol('x'),
             (symbol('compile/begin'), symbol('y')),
             )
        )

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_keyword_inside_brackets_does_not_terminate(self):
        src = """
        unknown [(foo) as: x]:
          y
        """

        expected = pycons(
            (symbol('unknown'),
             ((symbol('foo'),), keyword('as'), symbol('x')),
             (symbol('compile/begin'), symbol('y')),
             )
        )

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_try_except_only(self):
        src = """
        try:
          x
        except [Exception]:
          y
        """

        try_body = cons(symbol('compile/begin'), symbol('x'), nil)
        except_clause = cons(symbol('Exception'), symbol('y'), nil)
        expected = cons(symbol('try'), try_body, except_clause, nil)

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_try_full_block(self):
        src = """
        try:
          x
        except [Exception]:
          y
        else:
          a
        finally:
          z
        """

        try_body = cons(symbol('compile/begin'), symbol('x'), nil)
        except_clause = cons(symbol('Exception'), symbol('y'), nil)
        else_clause = cons(keyword('else'), symbol('a'), nil)
        finally_clause = cons(keyword('finally'), symbol('z'), nil)
        expected = cons(symbol('try'), try_body, except_clause,
                         else_clause, finally_clause, nil)

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)


class TestLayoutNgHeaderNestingParity(IsolatedAsyncioTestCase):

    async def test_block_header_s_expr_spanning_lines(self):
        src = """
        foo [[x 1000]
             [y 2000]]:
          (do_something x y)
        """

        expected = pycons(
          (symbol('foo'),
           ((symbol('x'), 1000),
            (symbol('y'), 2000),),
           (symbol('compile/begin'),
            (symbol('do_something'), symbol('x'), symbol('y')),
           )
          )
        )

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_block_header_keyword_inside_brackets(self):
        src = """
        unknown [(foo) as: x]:
          y
        """

        expected = pycons(
          (symbol('unknown'),
           ((symbol('foo'),), keyword('as'), symbol('x')),
           (symbol('compile/begin'), symbol('y')))
        )

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_block_header_keyword_inside_parens(self):
        src = """
        unknown (foo as: x):
          y
        """

        expected = pycons(
          (symbol('unknown'),
           (symbol('foo'), keyword('as'), symbol('x')),
           (symbol('compile/begin'), symbol('y')))
        )

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)


# The end.

