from unittest import TestCase

from koskari.types import keyword, symbol
from koskari.compile.helpers import pycons

from .. import parse_layout

_compile_cond = symbol('compile/cond')


class TestLayoutIfPeers(TestCase):

    def test_if_without_peers(self):
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


    def test_if_with_else_peer(self):
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


    def test_if_with_elif_then_else(self):
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


    def test_if_invalid_peer_order(self):
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
