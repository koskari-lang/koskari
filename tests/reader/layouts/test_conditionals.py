from unittest import IsolatedAsyncioTestCase

from koskari.lib import keyword, symbol
from koskari.compile.helpers import pycons

from .. import parse_layout

import pytest

pytestmark = pytest.mark.phase_infra

_compile_cond = symbol('cond')


class TestLayoutIfPeers(IsolatedAsyncioTestCase):

    async def test_if_without_peers(self):
        src = """
        if x:
          y
        """

        expected = pycons(
            (_compile_cond,
             (symbol('x'), symbol('y')))
        )

        result = parse_layout(src)

        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_if_with_else_peer(self):
        src = """
        if x:
          y
        else:
          z
        """

        expected = pycons(
            (_compile_cond,
             (symbol('x'), symbol('y')),
             (keyword('else'), symbol('z')))
        )

        result = parse_layout(src)

        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_if_with_else_peer_blank_line_before_else(self):
        """
        A blank line between the if body and a declared else: peer should not
        prevent peer gathering. Expected shape matches a normal if/else layout.
        """

        src = """
        if True:
          1

        else:
          2
        """

        expected = pycons(
            (_compile_cond,
             (True, 1),
             (keyword('else'), 2))
        )

        result = parse_layout(src)

        self.assertEqual(result, expected)


    async def test_comment_lines_are_blank_for_block_structure(self):
        """
        A line holding only whitespace and a comment is a blank line: its
        column is not an indent, so it neither ends a body nor blocks a
        declared peer, wherever it sits
        """

        expected = pycons(
            (_compile_cond,
             (True, 1),
             (keyword('else'), 2))
        )

        # column-0 comment inside the body used to end the block
        self.assertEqual(parse_layout("""
        if True:
          1
        ; a note at column zero
        else:
          2
        """), expected)

        # comment at body indent as the last body line before the peer
        self.assertEqual(parse_layout("""
        if True:
          1
          ;; last thing in the body
        else:
          2
        """), expected)

        # comment indented deeper than the body
        self.assertEqual(parse_layout("""
        if True:
          1
              ;; over-indented note
        else:
          2
        """), expected)

        # trailing comment on the last body line
        self.assertEqual(parse_layout("""
        if True:
          1  ; trailing
        else:
          2
        """), expected)

        # comment on the header line itself
        self.assertEqual(parse_layout("""
        if True:  ; header note
          1
        else:
          2
        """), expected)


    async def test_column_zero_comment_between_body_lines_keeps_body(self):
        expected = pycons(
            (_compile_cond,
             (True, 1, 2),
             (keyword('else'), 3))
        )

        self.assertEqual(parse_layout("""
        if True:
          1
        ; not a dedent
          2
        else:
          3
        """), expected)


    async def test_if_with_else_peer_blank_line_and_comment_before_else(self):
        """
        Blank lines and comment-only lines before a declared else: peer should
        not prevent peer gathering.
        """

        src = """
        if True:
          1

        ;; this is a comment
        else:
          2
        """

        expected = pycons(
            (_compile_cond,
             (True, 1),
             (keyword('else'), 2))
        )

        result = parse_layout(src)

        self.assertEqual(result, expected)


    async def test_if_with_elif_then_else(self):
        src = """
        if x:
          a
        elif y:
          b
        else:
          c
        """

        expected = pycons(
            (_compile_cond,
             (symbol('x'), symbol('a')),
             (symbol('y'), symbol('b')),
             (keyword('else'), symbol('c')))
        )

        result = parse_layout(src)

        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_if_invalid_peer_order(self):
        src = """
        if x:
          a
        else:
          b
        elif y:
          c
        """

        self.assertRaises(SyntaxError, parse_layout, src)


# The end.
