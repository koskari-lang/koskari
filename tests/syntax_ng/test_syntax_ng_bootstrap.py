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
Bootstrap tests for koskari.syntax_ng: parser, Syntax.__add__, Transform freeze
"""


import unittest

import pytest

from koskari.lib import Matcher, cons, nil, symbol
from koskari.syntax_ng import (
    MacroTransform,
    Syntax,
    SyntaxTransform,
    parse_syntax_rules_pattern,
)
from koskari.types import build_proper


pytestmark = pytest.mark.phase_infra

SYMBOL_TYPE = type(symbol("x"))


class TestParseSyntaxRulesPattern(unittest.TestCase):


    def test_bare_pattern_variable_is_rest_of_tail(self):
        """
        Whole pattern is one symbol not in literals -> GROUP(REST(slot))
        """

        x = symbol("x")
        lit = frozenset()
        p = parse_syntax_rules_pattern(x, lit, None)
        frozen = p.freeze()
        self.assertEqual(frozen[0], "MATCHER")
        m = Matcher.from_tuple(frozen)
        a = symbol("a")
        b = symbol("b")
        r = m.match((a, b))
        self.assertTrue(r.matched)
        self.assertEqual(r.captures["x"], (a, b))


    def test_literal_symbol_top_level(self):
        """
        Whole pattern is a literal symbol -> one spine element must equal it
        """

        q = symbol("quote")
        lit = frozenset({q})
        p = parse_syntax_rules_pattern(q, lit, None)
        m = Matcher.from_tuple(p.freeze())
        r = m.match((q,))
        self.assertTrue(r.matched)
        r2 = m.match((symbol("other"),))
        self.assertFalse(r2.matched)


    def test_sequence_and_ellipsis(self):
        """
        (a ... ) binds repeat_capture for a
        """

        a = symbol("a")
        lit = frozenset()
        pat = build_proper(a, Ellipsis)
        p = parse_syntax_rules_pattern(pat, lit, None)
        m = Matcher.from_tuple(p.freeze())
        r = m.match(())
        self.assertTrue(r.matched)
        self.assertEqual(r.captures["a"], [])
        s1 = symbol("s1")
        s2 = symbol("s2")
        r2 = m.match((s1, s2))
        self.assertTrue(r2.matched)
        frames = r2.captures["a"]
        self.assertEqual(len(frames), 2)
        self.assertEqual(frames[0]["a__sr"], s1)
        self.assertEqual(frames[1]["a__sr"], s2)


    def test_nested_sublist_pattern(self):
        """
        ((x)) matches one form whose interior is one capture x
        """

        x = symbol("x")
        inner = build_proper(x)
        pat = build_proper(inner)
        lit = frozenset()
        p = parse_syntax_rules_pattern(pat, lit, None)
        m = Matcher.from_tuple(p.freeze())
        sx = symbol("inner-val")
        form = build_proper(sx)
        r = m.match((form,), symbol=SYMBOL_TYPE)
        self.assertTrue(r.matched)
        self.assertEqual(r.captures["x"], sx)


    def test_match_wrapper_dispatches_inner(self):
        """
        (match INNER) uses INNER only
        """

        x = symbol("x")
        inner = build_proper(x)
        m_sym = symbol("match")
        wrapped = cons(m_sym, cons(inner, nil))
        lit = frozenset()
        p = parse_syntax_rules_pattern(wrapped, lit, None)
        m = Matcher.from_tuple(p.freeze())
        r = m.match((symbol("only"),), symbol=SYMBOL_TYPE)
        self.assertTrue(r.matched)
        self.assertEqual(r.captures["x"], symbol("only"))


class TestSyntaxAdd(unittest.TestCase):


    def test_add_transform_prepends(self):
        """
        self + other puts other's transform first
        """

        n = symbol("compile/foo")
        t1 = SyntaxTransform(parse_syntax_rules_pattern(symbol("a"), frozenset(), None))
        t2 = MacroTransform(parse_syntax_rules_pattern(symbol("b"), frozenset(), None))
        s = Syntax(n, (), (t1,))
        s2 = s + t2
        self.assertIsNot(s2, s)
        self.assertEqual(s2.transforms[0], t2)
        self.assertEqual(s2.transforms[1], t1)


    def test_add_syntax_prepends_all(self):
        """
        Syntax + Syntax prepends other's transforms as a block
        """

        n = symbol("compile/bar")
        ta = SyntaxTransform(parse_syntax_rules_pattern(symbol("a"), frozenset(), None))
        tb = SyntaxTransform(parse_syntax_rules_pattern(symbol("b"), frozenset(), None))
        tc = SyntaxTransform(parse_syntax_rules_pattern(symbol("c"), frozenset(), None))
        s1 = Syntax(n, (), (ta,))
        s2 = Syntax(n, (), (tb, tc))
        s3 = s1 + s2
        self.assertEqual(s3.transforms, (tb, tc, ta))


class TestSyntaxCompileImplStub(unittest.TestCase):


    def test_compile_impl_raises(self):
        n = symbol("compile/stub")
        s = Syntax(n, (), ())
        with self.assertRaises(NotImplementedError):
            s.compile_impl(None, nil, False)


class TestTransformFreeze(unittest.TestCase):


    def test_transform_has_matcher_tuple_and_hygiene(self):
        p = parse_syntax_rules_pattern(symbol("z"), frozenset(), None)
        t = SyntaxTransform(p)
        self.assertEqual(t.matcher_tuple[0], "MATCHER")
        self.assertEqual(len(t.matcher_tuple), 7)
        self.assertTrue(t.hygiene.is_empty())


class TestDatum(unittest.TestCase):


    def test_datum_iterates_like_syntax_box(self):
        from koskari.syntax_ng import Datum

        a = symbol("a")
        b = symbol("b")
        lst = build_proper(a, b)
        d = Datum(lst)
        out = list(d)
        self.assertEqual(len(out), 2)
        self.assertEqual(out[0].datum, a)
        self.assertEqual(out[1].datum, b)


# The end.
