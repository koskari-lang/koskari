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
tests.reader.test_layout_decorators

Layout _layout_macros @ prefix producing compile/decorate forms.
"""

from unittest import IsolatedAsyncioTestCase

from koskari.lib import keyword, symbol
from koskari.reader import LayoutReader, source_str
from koskari.reader.layouts import LAYOUTS
from koskari.compile.helpers import pycons
from koskari.exceptions import ReaderSyntaxError

from . import parse_layout

import pytest

pytestmark = pytest.mark.phase_infra


class TestLayoutDecorateBlock(IsolatedAsyncioTestCase):

    async def test_single_decorator_define_class(self):
        src = """
        @foo
        define-class Obj []:
          x
        """

        inner = pycons(
            (symbol('compile/define-class'),
             symbol('Obj'),
             (),
             (symbol('compile/begin'), symbol('x'))))

        expected = pycons(
            (symbol('compile/decorate'),
             (symbol('foo'),),
             inner))

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_two_decorators_implicit_call(self):
        src = """
        @foo
        @bar 123
        define-class Obj []:
          x
        """

        inner = pycons(
            (symbol('compile/define-class'),
             symbol('Obj'),
             (),
             (symbol('compile/begin'), symbol('x'))))

        expected = pycons(
            (symbol('compile/decorate'),
             (symbol('foo'), (symbol('bar'), 123)),
             inner))

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_decorator_explicit_sexpr_call(self):
        # @(…) must use the layout @ path, not matmul (@ as first token on line).
        src = """
        @(foo 123)
        define-class Obj []:
          x
        """

        inner = pycons(
            (symbol('compile/define-class'),
             symbol('Obj'),
             (),
             (symbol('compile/begin'), symbol('x'))))

        expected = pycons(
            (symbol('compile/decorate'),
             ((symbol('foo'), 123),),
             inner))

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_decorated_sexpr_inner(self):
        src = """
        @foo
        (list 1)
        """

        inner = pycons((symbol('list'), 1))

        expected = pycons(
            (symbol('compile/decorate'),
             (symbol('foo'),),
             inner))

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_decorated_if_with_else_peer(self):
        src = """
        @mark
        if cond:
          a
        else:
          b
        """

        inner = pycons(
            (symbol('compile/cond'),
             (symbol('cond'), symbol('a')),
             (keyword('else'), symbol('b'))))

        expected = pycons(
            (symbol('compile/decorate'),
             (symbol('mark'),),
             inner))

        result = parse_layout(src)
        self.assertEqual(result, expected)


class TestLayoutDecorateErrors(IsolatedAsyncioTestCase):

    async def test_empty_decorator_line_errors(self):
        src = """
        @
        define-class Obj []:
          x
        """

        builtins = {str(k): v for k, v in LAYOUTS.items()}
        reader = LayoutReader(builtins)
        stream = source_str(src, '<unittest>')

        with self.assertRaises(ReaderSyntaxError) as ctx:
            reader.read(stream)

        self.assertIn('empty decorator line', str(ctx.exception).lower())


class TestLayoutDecorateLayoutMacroDisabled(IsolatedAsyncioTestCase):

    async def test_without_at_layout_macro_atom_fallback(self):
        """
        Subclass omits @ from _layout_macros; @foo reads as a symbol-headed line
        """

        class LayoutReaderNoAt(LayoutReader):

            def _add_default_macros(self):
                super()._add_default_macros()
                del self._layout_macros['@']

        builtins = {}
        for name, value in LAYOUTS.items():
            builtins[str(name)] = value

        reader = LayoutReaderNoAt(builtins)
        stream = source_str('@foo\ndefine-class Obj []:\n  x\n', '<unittest>')
        result = reader.read(stream)

        self.assertTrue(str(result).startswith('@'))


# The end.
