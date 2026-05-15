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
Tests for ``Syntax`` construction via ``datum_syntax_helpers`` and
``build-syntax``-style rule rows
"""


import unittest

import pytest

from koskari.lib import Module, ModuleNamespace, cons, nil, symbol
from koskari.types import build_proper

from tests.syntax.datum_syntax_helpers import (
    macro_from_rule_rows,
    special_from_match_rows,
    special_from_rule_rows,
    syntax_from_rule_datums,
)


pytestmark = pytest.mark.phase_infra


class _StubCompiler(object):
    """
    Minimal compiler supplying find_compiled for template hygiene
    """

    def __init__(self):
        self.module = Module(ModuleNamespace('stub', {}))


    def find_compiled(self, sym):
        return None


class TestBuildSyntax(unittest.TestCase):

    def test_build_syntax_first_matching_arm_wins(self):
        """
        Same full form matches two variable arms; first listed arm wins
        """

        m = symbol("compile/m")
        rules = (
            (build_proper(m, symbol("x")), symbol("first")),
            (build_proper(m, symbol("y")), symbol("second")),
        )
        s = syntax_from_rule_datums(m, (), rules)
        comp = _StubCompiler()
        src = cons(m, symbol("z"), nil)
        out = s.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("first"))

    def test_build_syntax_second_arm_when_first_misses_structure(self):
        """
        First arm matches only (m onlyhere); three-cell form falls through to
        the wider arm
        """

        m = symbol("compile/m2")
        onlyhere = symbol("onlyhere")
        rules = (
            (build_proper(m, onlyhere), symbol("narrow")),
            (build_proper(m, symbol("a"), symbol("b")), symbol("wide")),
        )
        s = syntax_from_rule_datums(m, (onlyhere,), rules)
        comp = _StubCompiler()
        src = cons(m, symbol("solo"), symbol("extra"), nil)
        out = s.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("wide"))

    def test_build_syntax_empty_rules_raises(self):
        m = symbol("compile/m3")
        with self.assertRaises(ValueError):
            syntax_from_rule_datums(m, (), ())

    def test_build_syntax_bad_row_shape_raises(self):
        m = symbol("compile/m4")
        with self.assertRaises(ValueError):
            syntax_from_rule_datums(m, (), ((symbol("a"), symbol("b"), symbol("c")),))


class TestMacroFromRuleRows(unittest.TestCase):

    def test_macro_from_rule_rows_kwargs_invoke(self):
        """
        Macro procedures receive captures as keyword arguments plus tc
        """

        m = symbol("compile/mm")
        saw = []

        def proc(compiler, tc=None, **caps):
            saw.append((tc, caps))
            return symbol("macro-out")

        s = macro_from_rule_rows(m, (), ((build_proper(m, symbol("v")), proc),))
        comp = _StubCompiler()
        slot = symbol("slot")
        src = cons(m, slot, nil)
        out = s.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("macro-out"))
        self.assertEqual(len(saw), 1)
        tc, caps = saw[0]
        self.assertFalse(tc)
        self.assertEqual(caps["v"].datum, slot)

    def test_macro_from_rule_rows_three_tuple_row_raises(self):
        m = symbol("compile/mm2")

        def proc(compiler, tc=None, **caps):
            return symbol("done")

        with self.assertRaises(ValueError):
            macro_from_rule_rows(
                m, (),
                ((build_proper(m, symbol("w")), proc, None),))

    def test_macro_from_rule_rows_empty_raises(self):
        m = symbol("compile/mm3")
        with self.assertRaises(ValueError):
            macro_from_rule_rows(m, (), ())

    def test_macro_from_rule_rows_bad_row_len_raises(self):
        m = symbol("compile/mm4")
        with self.assertRaises(ValueError):
            macro_from_rule_rows(
                m, (),
                (
                    (
                        symbol("a"),
                        lambda c, tc=None, **kw: nil,
                        "extra",
                    ),
                ))


class TestSpecialFromRuleRows(unittest.TestCase):

    def test_special_from_rule_rows_handler_shape(self):
        m = symbol("compile/ss")

        def proc(compiler, source_obj, result, tc):
            return symbol("special-out")

        s = special_from_rule_rows(m, (), ((build_proper(m, symbol("q")), proc),))
        src = cons(m, symbol("arg"), nil)
        out = s.compile_impl(None, src, False)
        self.assertEqual(out, symbol("special-out"))

    def test_special_from_match_rows_uses_match_dialect_pattern(self):
        m = symbol("compile/sm")
        pat = build_proper(
            m, build_proper(symbol("SYMBOL"), symbol("r")))

        def proc(compiler, source_obj, result, tc):
            return symbol("match-out")

        s = special_from_match_rows(m, ((pat, proc),))
        src = cons(m, symbol("x"), nil)
        out = s.compile_impl(None, src, False)
        self.assertEqual(out, symbol("match-out"))

    def test_special_from_rule_rows_empty_raises(self):
        m = symbol("compile/ss2")
        with self.assertRaises(ValueError):
            special_from_rule_rows(m, (), ())


# The end.
