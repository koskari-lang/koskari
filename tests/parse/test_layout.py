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

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from unittest import TestCase

from koskari.types import (
    Module,
    ModuleNamespace,
    car,
    cdr,
    symbol,
)
from koskari.parse import LayoutReader, source_str

_layout_block = symbol('layout/block')


def _parse_layout(src_str):
    mod_ns = ModuleNamespace('test', {})
    module = Module(mod_ns)
    reader = LayoutReader(module)
    stream = source_str(src_str, '<unittest>')
    return reader.read(stream)


class TestLayoutBlockSyntax(TestCase):

    def test_simple_block(self):
        src = """
        if x :
          y
        """
        result = _parse_layout(src)
        self.assertTrue(result is not None)
        self.assertEqual(car(result), _layout_block)
        self.assertEqual(car(cdr(result)), symbol('if'))
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

    def test_nested_blocks(self):
        src = """
        let [[x 1]] :
          (do_something x)
        """
        result = _parse_layout(src)
        self.assertTrue(result is not None)
        self.assertEqual(car(result), _layout_block)
        self.assertEqual(car(cdr(result)), symbol('let'))
        body = car(cdr(cdr(cdr(result))))
        self.assertEqual(car(body), symbol('do_something'))

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


class TestLayoutReaderNamespace(TestCase):

    def test_accepts_module(self):
        mod_ns = ModuleNamespace('test', {})
        module = Module(mod_ns)
        reader = LayoutReader(module)
        self.assertIsNotNone(reader._ns)

    def test_accepts_dict(self):
        reader = LayoutReader({})
        self.assertIsNotNone(reader._ns)
