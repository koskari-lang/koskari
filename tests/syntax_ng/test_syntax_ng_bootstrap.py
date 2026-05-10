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

from koskari.exceptions import SpecialError
from koskari.lib import Matcher, cons, nil, symbol
from koskari.syntax_ng import (
    MacroTransform,
    SpecialTransform,
    Syntax,
    SyntaxTransform,
    Transform,
    _filter_captures,
    parse_syntax_rules_pattern,
    special,
)
from koskari.types import build_proper


pytestmark = pytest.mark.phase_infra


class TestParseSyntaxRulesPattern(unittest.TestCase):


    def test_bare_pattern_variable_is_rest_of_tail(self):
        """
        Whole pattern is one symbol not in literals -> MATCH(REST(slot))
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
        self.assertEqual(r.captures["x"].datum, (a, b))


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
        self.assertEqual(r.captures["a"].datum, [])
        s1 = symbol("s1")
        s2 = symbol("s2")
        r2 = m.match((s1, s2))
        self.assertTrue(r2.matched)
        frames = r2.captures["a"].datum
        self.assertEqual(len(frames), 2)
        self.assertEqual(frames[0]["a__sr"].datum, s1)
        self.assertEqual(frames[1]["a__sr"].datum, s2)


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
        r = m.match((form,), symbol=symbol)
        self.assertTrue(r.matched)
        self.assertEqual(r.captures["x"].datum, sx)


    def test_match_wrapper_dispatches_inner(self):
        """
        (match INNER) uses INNER only
        """

        x = symbol("x")
        inner = build_proper(x)
        m_sym = symbol("match")
        wrapped = cons(m_sym, inner, nil)
        lit = frozenset()
        p = parse_syntax_rules_pattern(wrapped, lit, None)
        m = Matcher.from_tuple(p.freeze())
        r = m.match((symbol("only"),), symbol=symbol)
        self.assertTrue(r.matched)
        self.assertEqual(
            r.captures["x"].datum, symbol("only"))


class TestSyntaxAdd(unittest.TestCase):


    def test_add_transform_prepends(self):
        """
        self + other puts other's transform first
        """

        n = symbol("compile/foo")
        t1 = SyntaxTransform(parse_syntax_rules_pattern(
            build_proper(n, symbol("a")), frozenset({n}), None))
        t2 = MacroTransform(
            parse_syntax_rules_pattern(
                build_proper(n, symbol("b")), frozenset({n}), None),
            lambda c, tc=None, **kw: nil)
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
        ta = SyntaxTransform(parse_syntax_rules_pattern(
            build_proper(n, symbol("a")), frozenset({n}), None))
        tb = SyntaxTransform(parse_syntax_rules_pattern(
            build_proper(n, symbol("b")), frozenset({n}), None))
        tc = SyntaxTransform(parse_syntax_rules_pattern(
            build_proper(n, symbol("c")), frozenset({n}), None))
        s1 = Syntax(n, (), (ta,))
        s2 = Syntax(n, (), (tb, tc))
        s3 = s1 + s2
        self.assertEqual(s3.transforms, (tb, tc, ta))


class TestSyntaxCompileImpl(unittest.TestCase):


    def test_compile_impl_no_match_raises_special_error(self):
        n = symbol("compile/stub")
        s = Syntax(n, (), ())
        with self.assertRaises(SpecialError):
            s.compile_impl(None, cons(n, nil), False)


    def test_compile_impl_first_arm_wins_when_both_patterns_match(self):
        n = symbol("compile/pick")
        p1 = parse_syntax_rules_pattern(
            build_proper(n, symbol("a")), frozenset({n}), None)
        p2 = parse_syntax_rules_pattern(
            build_proper(n, symbol("b")), frozenset({n}), None)
        seen = []

        def h1(c, s, mr, t):
            seen.append(1)
            return symbol("one")

        def h2(c, s, mr, t):
            seen.append(2)
            return symbol("two")

        s = Syntax(
            n,
            (),
            (
                SpecialTransform(p1, h1),
                SpecialTransform(p2, h2),
            ))
        src = cons(n, symbol("c"), nil)
        out = s.compile_impl(None, src, False)
        self.assertEqual(out, symbol("one"))
        self.assertEqual(seen, [1])


    def test_compile_impl_second_after_first_miss(self):
        n = symbol("compile/fallback")
        p_need_arg = parse_syntax_rules_pattern(
            build_proper(n, symbol("x")), frozenset({n}), None)
        p_rest = parse_syntax_rules_pattern(
            build_proper(n, symbol("a"), symbol("b")), frozenset({n}), None)

        def need_arg(c, s, mr, t):
            return symbol("narrow")

        def rest_z(c, s, mr, t):
            return symbol("wide")

        s = Syntax(
            n,
            (),
            (
                SpecialTransform(p_need_arg, need_arg),
                SpecialTransform(p_rest, rest_z),
            ))
        src = cons(n, symbol("z"), symbol("tail"), nil)
        out = s.compile_impl(None, src, False)
        self.assertEqual(out, symbol("wide"))


class TestTransformFreeze(unittest.TestCase):


    def test_transform_has_matcher_tuple_and_hygiene(self):
        zsym = symbol("compile/z")
        p = parse_syntax_rules_pattern(
            build_proper(zsym, symbol("z")), frozenset({zsym}), None)
        t = SyntaxTransform(p)
        self.assertEqual(t.matcher_tuple[0], "MATCHER")
        self.assertEqual(len(t.matcher_tuple), 6)
        self.assertTrue(t.hygiene.is_empty())


    def test_concrete_transforms_are_transform_protocol(self):
        qsym = symbol("compile/q")
        p = parse_syntax_rules_pattern(
            build_proper(qsym, symbol("q")), frozenset({qsym}), None)
        self.assertTrue(isinstance(SyntaxTransform(p), Transform))
        self.assertTrue(isinstance(
            MacroTransform(p, lambda c, tc=None, **kw: nil), Transform))
        self.assertTrue(isinstance(
            SpecialTransform(p, lambda c, s, t, mr: nil), Transform))


class TestSpecialUnpack(unittest.TestCase):


    def test_unpack_none_invokes_with_full_source(self):
        head = symbol("compile/unpack-none")
        p = parse_syntax_rules_pattern(
            build_proper(head, symbol("v")), frozenset({head}), None)
        saw = []

        def fn(compiler, source_obj, tc):
            saw.append(source_obj)
            return symbol("done")

        st = SpecialTransform(
            p,
            lambda compiler, source_obj, mr, tc: fn(compiler, source_obj, tc))
        src = cons(head, symbol("slot"), nil)
        out = st.try_expand(None, src, False)
        self.assertEqual(out, symbol("done"))
        self.assertEqual(len(saw), 1)
        self.assertIs(saw[0], src)


    def test_unpack_match_passes_capture_dict(self):
        head = symbol("compile/unpack-match")
        p = parse_syntax_rules_pattern(
            build_proper(head, symbol("v")), frozenset({head}), None)
        saw = []

        def fn(compiler, caps, tc):
            saw.append(caps)
            return symbol("done")

        st = SpecialTransform(
            p,
            lambda compiler, source_obj, mr, tc: fn(
                compiler, _filter_captures(mr.captures), tc))
        slot = symbol("slot")
        src = cons(head, slot, nil)
        out = st.try_expand(None, src, False)
        self.assertEqual(out, symbol("done"))
        self.assertEqual(len(saw), 1)
        self.assertEqual(saw[0]["v"].datum, slot)


    def test_unpack_kwargs_passes_tc_and_captures(self):
        head = symbol("compile/unpack-kw")
        p = parse_syntax_rules_pattern(
            build_proper(head, symbol("v")), frozenset({head}), None)
        saw = []

        def fn(compiler, tc=None, **caps):
            saw.append((tc, caps))
            return symbol("done")

        st = SpecialTransform(
            p,
            lambda compiler, source_obj, mr, tc: fn(
                compiler, tc=tc, **_filter_captures(mr.captures)))
        slot = symbol("slot")
        src = cons(head, slot, nil)
        out = st.try_expand(None, src, True)
        self.assertEqual(out, symbol("done"))
        self.assertEqual(saw[0][0], True)
        self.assertEqual(saw[0][1]["v"].datum, slot)


class TestSpecialDecorator(unittest.TestCase):


    def test_invalid_unpack_raises(self):
        u = symbol("compile/bad-unpack-base")
        p = parse_syntax_rules_pattern(
            build_proper(u, symbol("v")), frozenset({u}), None)
        with self.assertRaises(ValueError):
            special(symbol("compile/bad-unpack"), patterns=p, unpack="what")

        with self.assertRaises(ValueError):
            special(symbol("compile/bad-unpack2"), patterns=p, unpack="what")(
                lambda c, s, t: nil)


    def test_builds_syntax_with_one_or_many_patterns(self):
        d = symbol("compile/d")
        p1 = parse_syntax_rules_pattern(
            build_proper(d, symbol("a")), frozenset({d}), None)
        p2 = parse_syntax_rules_pattern(
            build_proper(d, symbol("b")), frozenset({d}), None)
        h = special(d, patterns=(p1, p2))(
            lambda c, s, t: nil)
        self.assertIsInstance(h, Syntax)
        self.assertEqual(len(h.transforms), 2)


class TestDatum(unittest.TestCase):


    def test_datum_iter_delegates_to_wrapped_pair(self):
        from koskari.lib import Datum

        a = symbol("a")
        b = symbol("b")
        lst = build_proper(a, b)
        d = Datum(lst)
        out = list(d)
        self.assertEqual(len(out), 2)
        self.assertIs(out[0], a)
        self.assertEqual(out[1], cons(b, nil))


# The end.
