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

Layout _layout_macros @ prefix producing decorate forms.
"""

from unittest import IsolatedAsyncioTestCase

import pytest

import koskari.compile.specials as _koskari_compile_specials  # noqa: F401
import koskari.reader.layouts as _koskari_reader_layouts  # noqa: F401
from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.compile.helpers import pycons
from koskari.exceptions import ReaderSyntaxError
from koskari.lib import keyword, symbol
from koskari.reader import IndentAwareStream, LayoutReader, source_str

from . import parse_layout


pytestmark = pytest.mark.phase_infra


class TestLayoutDecorateBlock(IsolatedAsyncioTestCase):

    async def test_single_decorator_define_class(self):
        src = """
        @foo
        class Obj []:
          x
        """

        inner = pycons(
            (symbol('class'),
             symbol('Obj'),
             (),
             (symbol('begin'), symbol('x'))))

        expected = pycons(
            (symbol('decorate'),
             (symbol('foo'),),
             inner))

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_two_decorators_implicit_call(self):
        src = """
        @foo
        @bar 123
        class Obj []:
          x
        """

        inner = pycons(
            (symbol('class'),
             symbol('Obj'),
             (),
             (symbol('begin'), symbol('x'))))

        expected = pycons(
            (symbol('decorate'),
             (symbol('foo'), (symbol('bar'), 123)),
             inner))

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_decorator_explicit_sexpr_call(self):
        # @(…) must use the layout @ path, not matmul (@ as first token on line).
        src = """
        @(foo 123)
        class Obj []:
          x
        """

        inner = pycons(
            (symbol('class'),
             symbol('Obj'),
             (),
             (symbol('begin'), symbol('x'))))

        expected = pycons(
            (symbol('decorate'),
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
            (symbol('decorate'),
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
            (symbol('cond'),
             (symbol('cond'), symbol('a')),
             (keyword('else'), symbol('b'))))

        expected = pycons(
            (symbol('decorate'),
             (symbol('mark'),),
             inner))

        result = parse_layout(src)
        self.assertEqual(result, expected)


class TestLayoutDecorateErrors(IsolatedAsyncioTestCase):

    async def test_empty_decorator_line_errors(self):
        src = """
        @
        class Obj []:
          x
        """

        builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(builtins)
        reader.set_layout_modes(layout_decorators=True)
        stream = IndentAwareStream.from_source(source_str(src, '<unittest>'))

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

        builtins = dict(_BUILTIN_REGISTRY)

        reader = LayoutReaderNoAt(builtins)
        stream = IndentAwareStream.from_source(
            source_str('@foo\ndefine-class Obj []:\n  x\n', '<unittest>'))
        result = reader.read(stream)

        self.assertTrue(str(result).startswith('@'))


class TestDecoratorPragmaGate(IsolatedAsyncioTestCase):
    """
    The @ layout macro is gated by the reader/decorators pragma: off on a
    raw reader, on at the pragma baseline, and declarable off per module.
    """

    SRC = '@foo\ndefine-class Obj []:\n  x\n'


    async def test_bare_reader_reads_at_as_atom(self):
        """
        A directly-constructed reader has no decorator surface; a leading
        @ line reads through the ordinary atom path
        """

        reader = LayoutReader(dict(_BUILTIN_REGISTRY))
        stream = IndentAwareStream.from_source(
            source_str(self.SRC, '<unittest>'))
        result = reader.read(stream)

        self.assertTrue(str(result).startswith('@'))


    async def test_pragma_baseline_enables_decorators(self):
        """
        A reader built from a default pragma lowers @ lines to
        decorate
        """

        reader = LayoutReader.from_pragma(dict(_BUILTIN_REGISTRY))
        stream = IndentAwareStream.from_source(
            source_str(self.SRC, '<unittest>'))
        result = reader.read(stream)

        head, _rest = result
        self.assertEqual(head, symbol('decorate'))


    async def test_pragma_declared_off_disables_decorators(self):
        """
        reader/decorators False in the pragma turns the surface off
        """

        from koskari.pragma import ModulePragma

        pragma = ModulePragma({"reader/decorators": False})
        reader = LayoutReader.from_pragma(
            dict(_BUILTIN_REGISTRY), pragma=pragma)
        stream = IndentAwareStream.from_source(
            source_str(self.SRC, '<unittest>'))
        result = reader.read(stream)

        self.assertTrue(str(result).startswith('@'))


# The end.
