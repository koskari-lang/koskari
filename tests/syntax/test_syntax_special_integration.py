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
Integration tests for syntax.special: full source form matching on pairs
"""


import unittest

import pytest

from koskari.exceptions import SyntaxMatchError
from koskari.lib import Module, ModuleNamespace, cons, kos_repr, nil, symbol
from koskari.matcher import CALLEDBY, MATCH, SYMBOL, VALUE
from koskari.syntax import special


pytestmark = pytest.mark.phase_infra


class _StubCompiler(object):
    """
    Minimal compiler for Syntax.compile_impl
    """

    def __init__(self):
        self.module = Module(ModuleNamespace('stub', {}))


    def find_compiled(self, sym):
        return None


class TestSpecialFullFormIntegration(unittest.TestCase):


    def test_special_matches_full_form_head_and_value(self):
        """
        Matcher sees the full cons spine; head and argument captured
        """

        primary = symbol("compile/ng-int")
        pat = MATCH(
            SYMBOL("head"),
            VALUE("x"),
        )

        @special(primary, patterns=pat, unpack="match")
        def _fn(compiler, caps):
            return (
                caps["head"].datum,
                caps["x"].datum,
            )

        alias = symbol("define/ng-int")
        arg = symbol("argval")
        src = cons(alias, arg, nil)
        comp = _StubCompiler()
        out = _fn.compile_impl(comp, src, True)
        self.assertEqual(out[0], alias)
        self.assertEqual(out[1], arg)


    def test_special_first_arm_wins_when_both_match(self):
        """
        Two MATCH arms on the same shape; first listed wins
        """

        primary = symbol("compile/ng-disj")
        arm1 = MATCH(SYMBOL("head"), VALUE("a"))
        arm2 = MATCH(SYMBOL("head"), VALUE("b"))

        seen = []

        def fn(compiler, caps):
            seen.append(frozenset(caps.keys()))
            return symbol("ok")

        h = special(primary, patterns=(arm1, arm2), unpack="match")(fn)

        src = cons(symbol("any-head"), symbol("cell"), nil)
        comp = _StubCompiler()
        out = h.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("ok"))
        self.assertEqual(len(seen), 1)
        self.assertIn("a", seen[0])
        self.assertNotIn("b", seen[0])


    def test_special_no_match_raises_syntax_match_error(self):
        """
        No arm matches the full form
        """

        primary = symbol("compile/ng-miss")
        pat = MATCH(CALLEDBY(), VALUE("x"), VALUE("y"))

        h = special(primary, patterns=pat, unpack="match")(
            lambda c, caps, tc: symbol("never"))

        src = cons(symbol("h"), symbol("only-one"), nil)
        comp = _StubCompiler()
        with self.assertRaises(SyntaxMatchError) as ctx:
            h.compile_impl(comp, src, False)

        exc = ctx.exception
        self.assertIs(exc.syntax, h)
        self.assertEqual(len(exc.patterns), 1)
        self.assertIs(exc.patterns[0], pat)
        explained = exc.explain()
        self.assertIn(kos_repr(src), explained)
        self.assertIn("accepts:", explained)
        self.assertIn("(h (VALUE x) (VALUE y))", explained)
        self.assertNotIn("(MATCH", explained)
        self.assertNotIn("(CALLEDBY)", explained)


    def test_improper_list_raises_type_error(self):
        """
        Non-nil-terminated spine is rejected when building the subject
        """

        primary = symbol("compile/ng-improper")
        pat = MATCH(SYMBOL("head"), VALUE("x"))

        h = special(primary, patterns=pat, unpack="match")(
            lambda c, caps, tc: symbol("unreachable"))

        a = symbol("a")
        b = symbol("b")
        bad = cons(a, b)
        comp = _StubCompiler()
        with self.assertRaises(TypeError) as ctx:
            h.compile_impl(comp, bad, False)
        self.assertIn("proper list", str(ctx.exception).lower())


    def test_unpack_none_passes_identity_source_obj(self):
        """
        Handler receives the same source pair object
        """

        primary = symbol("compile/ng-none")
        pat = MATCH(SYMBOL("head"), VALUE("x"))

        saw = []

        @special(primary, patterns=pat, unpack=None)
        def _fn(compiler, source_obj):
            saw.append(source_obj)
            return symbol("ok")

        src = cons(symbol("op"), symbol("u"), nil)
        comp = _StubCompiler()
        out = _fn.compile_impl(comp, src, False)
        self.assertEqual(out, symbol("ok"))
        self.assertEqual(len(saw), 1)
        self.assertIs(saw[0], src)


# The end.
