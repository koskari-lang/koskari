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
tests.reader.test_format_string

Reader and eval tests for f-string format fields

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from unittest import IsolatedAsyncioTestCase

import pytest

from koskari.lib import keyword, symbol
from koskari.pragma import ModulePragma
from koskari.reader import Reader, source_str
from koskari.types import unpack


pytestmark = pytest.mark.phase_infra


_symbol_str = symbol("#str")
_symbol_format = symbol("format")


def read_fstring(src):
    """
    Read one f-string source form
    """

    reader = Reader()
    stream = source_str(src, "<unittest>")
    return reader.read(stream)


def read_fstring_pragma(src):
    """
    Read one f-string form through a reader brought up to the default module
    pragma the way a real ``.ki`` module reads

    A directly-constructed reader is bare (suffix syntax off); applying a
    default ``ModulePragma`` turns on the settings a module gets by default,
    including ``reader/index-brackets``.
    """

    reader = Reader()
    reader.reload_pragma(ModulePragma())
    stream = source_str(src, "<unittest>")
    return reader.read(stream)


def fstring_parts(form):
    """
    Return the #str argument list from a read f-string form
    """

    called_by, body = form
    assert called_by is _symbol_str
    return list(unpack(body))


def format_field_args(field):
    """
    Return the argument list from a (format ...) field form
    """

    called_by, body = field
    assert called_by is _symbol_format
    return list(unpack(body))


class TestFormatStringReader(IsolatedAsyncioTestCase):

    async def test_body_starting_with_shebang_is_literal_text(self):
        """
        The body is string text, not a script: a leading #! is kept
        """

        form = read_fstring('f"#!/usr/bin/env python"')
        parts = fstring_parts(form)
        self.assertEqual(parts, ["#!/usr/bin/env python"])

    async def test_body_starting_with_shebang_keeps_its_field(self):
        form = read_fstring('f"#!{x}"')
        parts = fstring_parts(form)
        self.assertEqual(len(parts), 2)
        self.assertEqual(parts[0], "#!")
        self.assertEqual(format_field_args(parts[1]), [symbol("x")])

    async def test_single_arg_field(self):
        form = read_fstring('f"{x}"')
        parts = fstring_parts(form)
        self.assertEqual(len(parts), 1)
        self.assertEqual(format_field_args(parts[0]), [symbol("x")])

    async def test_colon_constant_spec(self):
        form = read_fstring('f"{x:04d}"')
        parts = fstring_parts(form)
        self.assertEqual(format_field_args(parts[0]), [symbol("x"), "04d"])

    async def test_colon_spec_on_list_expr(self):
        form = read_fstring('f"{(a b):.2f}"')
        parts = fstring_parts(form)
        args = format_field_args(parts[0])
        self.assertEqual(list(unpack(args[0])), [symbol("a"), symbol("b")])
        self.assertEqual(args[1], ".2f")

    async def test_colon_inside_expr_not_field_spec(self):
        form = read_fstring('f"{(a b: 1)}"')
        parts = fstring_parts(form)
        args = format_field_args(parts[0])
        self.assertEqual(len(args), 1)
        self.assertEqual(
            list(unpack(args[0])),
            [symbol("a"), keyword("b"), 1])

    async def test_legacy_constant_spec(self):
        form = read_fstring('f"{i \\"3d\\"}"')
        parts = fstring_parts(form)
        self.assertEqual(format_field_args(parts[0]), [symbol("i"), "3d"])

    async def test_legacy_dynamic_spec(self):
        form = read_fstring('f"{x rules}"')
        parts = fstring_parts(form)
        self.assertEqual(
            format_field_args(parts[0]),
            [symbol("x"), symbol("rules")])

    async def test_escaped_braces(self):
        form = read_fstring('f"{{literal}}"')
        parts = fstring_parts(form)
        self.assertEqual(parts, ['{', 'literal', '}'])

    async def test_escape_producing_brace_is_literal(self):
        # \x7b / \x7d decode to { and } but must not act as field
        # delimiters; the whole thing is a literal "{x}" string.
        form = read_fstring(r'f"\x7bx\x7d"')
        parts = fstring_parts(form)
        self.assertEqual(parts, ['{x}'])

    async def test_escape_before_field_decodes_in_literal(self):
        form = read_fstring(r'f"a\nb{x}"')
        parts = fstring_parts(form)
        self.assertEqual(parts[0], 'a\nb')
        self.assertEqual(format_field_args(parts[1]), [symbol('x')])

    async def test_colon_spec_with_nested_braces(self):
        form = read_fstring('f"{x:{w}}"')
        parts = fstring_parts(form)
        self.assertEqual(format_field_args(parts[0]), [symbol("x"), "{w}"])

    async def test_unary_minus_on_name_in_field(self):
        """
        A field reads a unary prefix the way bare source does: -x is
        (- x), not the symbol -x
        """

        form = read_fstring_pragma('f"{-x}"')
        args = format_field_args(fstring_parts(form)[0])
        self.assertEqual(len(args), 1)
        self.assertEqual(list(unpack(args[0])), [symbol("-"), symbol("x")])

    async def test_unary_minus_on_dotted_name_in_field(self):
        form = read_fstring_pragma('f"{-x.y}"')
        args = format_field_args(fstring_parts(form)[0])
        self.assertEqual(len(args), 1)
        op, operand = list(unpack(args[0]))
        self.assertEqual(op, symbol("-"))
        self.assertEqual(
            list(unpack(operand)), [symbol("attr"), symbol("x"), symbol("y")])

    async def test_unary_prefix_keeps_the_colon_spec_in_field(self):
        for src in ('f"{-x:04d}"', 'f"{+x:04d}"', 'f"{~x:04d}"'):
            form = read_fstring_pragma(src)
            args = format_field_args(fstring_parts(form)[0])
            self.assertEqual(len(args), 2, src)
            self.assertEqual(
                list(unpack(args[0])), [symbol(src[3]), symbol("x")], src)
            self.assertEqual(args[1], "04d", src)

    async def test_negative_literal_in_field_stays_a_number(self):
        form = read_fstring_pragma('f"{-1}"')
        args = format_field_args(fstring_parts(form)[0])
        self.assertEqual(args, [-1])

    async def test_bracket_suffix_attaches_in_field(self):
        # a bare index-bracket suffix in a slot must attach as a subscript,
        # not land in the format-spec position as a separate form.
        form = read_fstring_pragma('f"{foo[0]}"')
        parts = fstring_parts(form)
        args = format_field_args(parts[0])
        self.assertEqual(len(args), 1)
        self.assertEqual(
            list(unpack(args[0])),
            [symbol("item"), symbol("foo"), 0])

    async def test_bracket_suffix_chains_in_field(self):
        form = read_fstring_pragma('f"{grid[y][x]}"')
        parts = fstring_parts(form)
        args = format_field_args(parts[0])
        self.assertEqual(len(args), 1)
        outer = list(unpack(args[0]))
        self.assertEqual(outer[0], symbol("item"))
        self.assertEqual(outer[2], symbol("x"))
        self.assertEqual(
            list(unpack(outer[1])),
            [symbol("item"), symbol("grid"), symbol("y")])

    async def test_bracket_suffix_with_colon_spec_in_field(self):
        # subscript attaches, then a colon still opens the format spec.
        form = read_fstring_pragma('f"{foo[0]:3d}"')
        parts = fstring_parts(form)
        args = format_field_args(parts[0])
        self.assertEqual(len(args), 2)
        self.assertEqual(
            list(unpack(args[0])),
            [symbol("item"), symbol("foo"), 0])
        self.assertEqual(args[1], "3d")

    async def test_bracket_suffix_before_spec_in_field(self):
        # the subscript attaches, and a following space-separated form is
        # still the format spec.
        form = read_fstring_pragma('f"{foo[0] \\"3d\\"}"')
        parts = fstring_parts(form)
        args = format_field_args(parts[0])
        self.assertEqual(len(args), 2)
        self.assertEqual(
            list(unpack(args[0])),
            [symbol("item"), symbol("foo"), 0])
        self.assertEqual(args[1], "3d")


class TestFormatStringEval(IsolatedAsyncioTestCase):

    async def test_colon_spec_eval(self):
        from tests.compile.specials import SpecialsTestCase

        case = SpecialsTestCase()
        case.setUp()
        expr, mod = case.compile_expr('f"{42:04d}"', format=format)
        self.assertEqual(await expr(), "0042")

    async def test_legacy_dynamic_spec_eval(self):
        from tests.compile.specials import SpecialsTestCase

        case = SpecialsTestCase()
        case.setUp()
        expr, mod = case.compile_expr(
            'f"{42 rules}"',
            format=format,
            rules="04d")
        self.assertEqual(await expr(), "0042")


# The end.
