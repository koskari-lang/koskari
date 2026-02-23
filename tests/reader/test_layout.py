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
tests.parse.test_layout

Unit tests for koskari.parse LayoutReader and block syntax.
"""


from unittest import TestCase

from koskari.types import (
    Module,
    ModuleNamespace,
    car,
    cdr,
    is_pair,
    symbol,
    keyword,
)
from koskari.reader import LayoutReader, source_str
from koskari.compile.helpers import pycons

_layout_block = symbol('layout/block')
_layout_if = symbol('layout/if')


def _parse_layout(src_str):
    mod_ns = ModuleNamespace('test', {})
    module = Module(mod_ns)
    reader = LayoutReader(module)
    stream = source_str(src_str, '<unittest>')
    return reader.read(stream)


class TestLayoutBlockSyntax(TestCase):

    def test_simple_block(self):
        src = """
        foo x :
          y
        """
        result = _parse_layout(src)
        self.assertTrue(result is not None)
        self.assertEqual(car(result), _layout_block)
        self.assertEqual(car(cdr(result)), symbol('foo'))
        self.assertEqual(car(cdr(cdr(result))), symbol('x'))
        self.assertEqual(car(cdr(cdr(cdr(result)))), symbol('y'))

    def test_block_header_s_expr_spanning_lines(self):
        src = """
        let [[x 1000]
             [y 2000]] :
          (do_something x y)
        """
        result = _parse_layout(src)
        self.assertTrue(result is not None)
        self.assertEqual(car(result), _layout_block)
        self.assertEqual(car(cdr(result)), symbol('let'))
        body = car(cdr(cdr(cdr(result))))
        self.assertEqual(car(body), symbol('do_something'))

    def test_nested_block_inside_block(self):
        src = """
        let [[x 1]] :
          if x :
            (foo x)
        """
        result = _parse_layout(src)

        self.assertTrue(result is not None)

        expected = pycons((
            _layout_block,
            symbol('let'),
            ((symbol('x'), 1),),
            (_layout_if,
                (symbol('x'), (symbol('foo'), symbol('x'),),),)
        ))

        self.assertEqual(result, expected)

    def test_dedent_ends_body(self):
        src = """
        foo x :
          a
          b
        bar
        """
        result = _parse_layout(src)
        self.assertTrue(result is not None)
        body = car(cdr(cdr(cdr(result))))
        from koskari.types import unpack
        body_forms = list(unpack(body))
        self.assertIn(symbol('a'), body_forms)
        self.assertIn(symbol('b'), body_forms)

    def test_fallback_no_layout_head(self):
        src = """
        unknown x :
          (print x)
        """
        result = _parse_layout(src)
        self.assertEqual(car(result), _layout_block)
        self.assertEqual(car(cdr(result)), symbol('unknown'))


class TestLayoutIfPeers(TestCase):

    def test_if_without_peers(self):
        src = """
        if x :
          y
        """
        result = _parse_layout(src)
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
        result = _parse_layout(src)
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
        result = _parse_layout(src)
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
        self.assertRaises(SyntaxError, _parse_layout, src)


class TestLayoutReaderNamespace(TestCase):

    def test_accepts_module(self):
        mod_ns = ModuleNamespace('test', {})
        module = Module(mod_ns)
        reader = LayoutReader(module)
        self.assertIsNotNone(reader._ns)

    def test_accepts_dict(self):
        reader = LayoutReader({})
        self.assertIsNotNone(reader._ns)


# The end.
