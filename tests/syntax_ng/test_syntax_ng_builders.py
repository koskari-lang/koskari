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
Tests for koskari.syntax_ng builder functions (build_syntax and relatives).
"""


import unittest

import pytest

from koskari.lib import cons, nil, symbol
from koskari.types import build_proper
from koskari.syntax_ng import (
    build_macro_syntax,
    build_special_match,
    build_special_syntax,
    build_syntax,
)


pytestmark = pytest.mark.phase_infra


class _StubCompiler(object):
    """
    Minimal compiler supplying find_compiled for template hygiene
    """

    def find_compiled(self, sym):
        return None


class TestBuildSyntax(unittest.TestCase):

    def test_build_syntax_first_matching_arm_wins(self):
        """
        Same tail matches multiple REST patterns; first listed arm wins
        """

        m = symbol("compile/m")
        rules = (
            (symbol("x"), symbol("first")),
            (symbol("y"), symbol("second")),
        )
        s = build_syntax(m, (), rules)
        comp = _StubCompiler()
        src = cons(m, symbol("z"), nil)
        out = s.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("first"))

    def test_build_syntax_second_arm_when_first_misses_structure(self):
        """
        First arm matches only a single literal tail slot; two-arg tail falls
        through to the REST arm
        """

        m = symbol("compile/m2")
        onlyhere = symbol("onlyhere")
        rules = (
            (build_proper(onlyhere), symbol("narrow")),
            (symbol("z"), symbol("wide")),
        )
        s = build_syntax(m, (onlyhere,), rules)
        comp = _StubCompiler()
        src = cons(m, symbol("solo"), symbol("extra"), nil)
        out = s.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("wide"))

    def test_build_syntax_empty_rules_raises(self):
        m = symbol("compile/m3")
        with self.assertRaises(ValueError):
            build_syntax(m, (), ())

    def test_build_syntax_bad_row_shape_raises(self):
        m = symbol("compile/m4")
        with self.assertRaises(ValueError):
            build_syntax(m, (), ((symbol("a"), symbol("b"), symbol("c")),))


class TestBuildMacroSyntax(unittest.TestCase):

    def test_build_macro_syntax_kwargs_default(self):
        """
        Default unpack passes captures as keyword arguments
        """

        m = symbol("compile/mm")
        saw = []

        def proc(compiler, tc=None, **caps):
            saw.append((tc, caps))
            return symbol("macro-out")

        s = build_macro_syntax(m, (), ((symbol("v"), proc),))
        comp = _StubCompiler()
        slot = symbol("slot")
        src = cons(m, slot, nil)
        out = s.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("macro-out"))
        self.assertEqual(len(saw), 1)
        tc, caps = saw[0]
        self.assertFalse(tc)
        self.assertEqual(caps["v"], (slot,))

    def test_build_macro_syntax_unpack_none_three_tuple(self):
        m = symbol("compile/mm2")
        saw = []

        def proc(compiler, source_obj, tc):
            saw.append(source_obj)
            return symbol("done")

        s = build_macro_syntax(
            m, (),
            ((symbol("w"), proc, None),))
        comp = _StubCompiler()
        src = cons(m, symbol("u"), nil)
        out = s.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("done"))
        self.assertEqual(len(saw), 1)
        self.assertIs(saw[0], src)

    def test_build_macro_syntax_empty_raises(self):
        m = symbol("compile/mm3")
        with self.assertRaises(ValueError):
            build_macro_syntax(m, (), ())

    def test_build_macro_syntax_bad_row_len_raises(self):
        m = symbol("compile/mm4")
        with self.assertRaises(ValueError):
            build_macro_syntax(
                m, (),
                ((symbol("a"), lambda c: nil, None, "extra"),))


class TestBuildSpecialSyntax(unittest.TestCase):

    def test_build_special_syntax_default_unpack(self):
        m = symbol("compile/ss")

        def proc(compiler, source_obj, tc):
            return symbol("special-out")

        s = build_special_syntax(m, (), ((symbol("q"), proc),))
        src = cons(m, symbol("arg"), nil)
        out = s.compile_impl(None, src, False)
        self.assertEqual(out, symbol("special-out"))

    def test_build_special_match_delegates_empty_literals(self):
        m = symbol("compile/sm")

        def proc(compiler, source_obj, tc):
            return symbol("match-out")

        s = build_special_match(m, ((symbol("r"), proc),))
        src = cons(m, symbol("x"), nil)
        out = s.compile_impl(None, src, False)
        self.assertEqual(out, symbol("match-out"))

    def test_build_special_syntax_empty_raises(self):
        m = symbol("compile/ss2")
        with self.assertRaises(ValueError):
            build_special_syntax(m, (), ())


# The end.
