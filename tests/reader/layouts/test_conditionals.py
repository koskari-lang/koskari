from unittest import TestCase

from koskari.types import car, cdr, symbol, keyword

from .. import parse_layout


_layout_if = symbol('layout/if')


class TestLayoutIfPeers(TestCase):

    def test_if_without_peers(self):
        src = """
        if x :
          y
        """
        result = parse_layout(src)
        self.assertEqual(car(result), _layout_if)
        first_clause = car(cdr(result))
        self.assertEqual(car(first_clause), symbol('x'))
        self.assertEqual(car(cdr(first_clause)), symbol('y'))

    def test_if_with_else_peer(self):
        src = """
        if x :
          y
        else :
          z
        """
        result = parse_layout(src)
        self.assertEqual(car(result), _layout_if)

        first = car(cdr(result))
        second = car(cdr(cdr(result)))

        self.assertEqual(car(first), symbol('x'))
        self.assertEqual(car(cdr(first)), symbol('y'))
        self.assertEqual(car(second), keyword('else'))
        self.assertEqual(car(cdr(second)), symbol('z'))

    def test_if_with_elif_then_else(self):
        src = """
        if x :
          a
        elif y :
          b
        else :
          c
        """
        result = parse_layout(src)
        self.assertEqual(car(result), _layout_if)

        first = car(cdr(result))
        second = car(cdr(cdr(result)))
        third = car(cdr(cdr(cdr(result))))

        self.assertEqual(car(first), symbol('x'))
        self.assertEqual(car(cdr(first)), symbol('a'))
        self.assertEqual(car(second), symbol('y'))
        self.assertEqual(car(cdr(second)), symbol('b'))
        self.assertEqual(car(third), keyword('else'))
        self.assertEqual(car(cdr(third)), symbol('c'))

    def test_if_invalid_peer_order(self):
        src = """
        if x :
          a
        else :
          b
        elif y :
          c
        """
        self.assertRaises(SyntaxError, parse_layout, src)


# The end.
