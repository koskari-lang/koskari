# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
tests.compile.helpers.test_helpers_gather

Contract tests for gather_parameters and gather_formals (matcher dict
payload)

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile.helpers import (
    FORMALS,
    gather_formals,
    gather_parameters,
    match_pattern_raw,
)
from koskari.exceptions import ParametersError, SpecialError
from koskari.lib import Datum, cons, keyword, nil, symbol


pytestmark = pytest.mark.phase_compile


class TestGatherParametersNg(unittest.TestCase):


    def test_positional_only(self):
        args = cons(symbol("a"), symbol("b"), nil)
        args_seq, kwds_seq = gather_parameters(args)
        self.assertEqual(args_seq, [(0, symbol("a")), (1, symbol("b"))])
        self.assertEqual(kwds_seq, [])

    def test_empty_list(self):
        args_seq, kwds_seq = gather_parameters(nil)
        self.assertEqual(args_seq, [])
        self.assertEqual(kwds_seq, [])

    def test_single_symbol_rest(self):
        rest = symbol("args")
        args_seq, kwds_seq = gather_parameters(rest)
        self.assertEqual(args_seq, [("*", rest)])
        self.assertEqual(kwds_seq, [])

    def test_keywords(self):
        k1 = keyword("k1")
        k2 = keyword("k2")
        v1 = symbol("v1")
        v2 = symbol("v2")
        args = cons(k1, v1, k2, v2, nil)
        args_seq, kwds_seq = gather_parameters(args)
        self.assertEqual(args_seq, [])
        self.assertEqual(kwds_seq, [("k1", v1), ("k2", v2)])

    def test_positional_then_keywords(self):
        a = symbol("a")
        b = symbol("b")
        k = keyword("x")
        v = symbol("v")
        args = cons(a, b, k, v, nil)
        args_seq, kwds_seq = gather_parameters(args)
        self.assertEqual(args_seq, [(0, a), (1, b)])
        self.assertEqual(kwds_seq, [("x", v)])

    def test_improper_list_rest(self):
        a = symbol("a")
        rest = symbol("rest")
        args = cons(a, rest)
        args_seq, kwds_seq = gather_parameters(args)
        self.assertEqual(args_seq, [(0, a), ("*", rest)])
        self.assertEqual(kwds_seq, [])

    def test_star_starstar(self):
        star_sym = symbol("args")
        starstar_sym = symbol("kwargs")
        k = keyword("*")
        kk = keyword("**")
        args = cons(k, star_sym, kk, starstar_sym, nil)
        args_seq, kwds_seq = gather_parameters(args)
        self.assertEqual(args_seq, [("*", star_sym)])
        self.assertEqual(kwds_seq, [("**", starstar_sym)])

    def test_interleaved_positional_and_star(self):
        a = symbol("a")
        bargs = symbol("bargs")
        c = symbol("c")
        dargs = symbol("dargs")
        k_star = keyword("*")
        args = cons(a, k_star, bargs, c, k_star, dargs, nil)
        args_seq, kwds_seq = gather_parameters(args)
        self.assertEqual(
            args_seq,
            [(0, a), ("*", bargs), (1, c), ("*", dargs)])
        self.assertEqual(kwds_seq, [])

    def test_interleaved_keyword_and_starstar(self):
        x = keyword("x")
        xval = symbol("xval")
        kw1 = symbol("kw1")
        y = keyword("y")
        yval = symbol("yval")
        kw2 = symbol("kw2")
        kk = keyword("**")
        args = cons(x, xval, kk, kw1, y, yval, kk, kw2, nil)
        args_seq, kwds_seq = gather_parameters(args)
        self.assertEqual(args_seq, [])
        self.assertEqual(
            kwds_seq,
            [("x", xval), ("**", kw1), ("y", yval), ("**", kw2)])

    def test_missing_value_for_keyword_raises(self):
        args = cons(keyword("a"), nil)
        with self.assertRaises(ParametersError) as ctx:
            gather_parameters(args)
        self.assertIn("keyword", str(ctx.exception).lower())

    def test_star_after_keyword_raises(self):
        a = symbol("a")
        k = keyword("a")
        v = symbol("v")
        k_star = keyword("*")
        star_sym = symbol("foo")
        args = cons(a, k, v, k_star, star_sym, nil)
        with self.assertRaises(ParametersError) as ctx:
            gather_parameters(args)
        self.assertIn("*", str(ctx.exception))
        self.assertIn("before keyword", str(ctx.exception).lower())

    def test_positional_after_keyword_raises(self):
        k = keyword("x")
        v = symbol("v")
        extra = symbol("extra")
        args = cons(k, v, extra, nil)
        with self.assertRaises(ParametersError) as ctx:
            gather_parameters(args)
        self.assertIn("positional", str(ctx.exception).lower())
        self.assertIn("before keyword", str(ctx.exception).lower())

    def test_not_symbol_or_pair_raises(self):
        with self.assertRaises(ParametersError):
            gather_parameters(42)


def _match_formals_group(spine):
    caps = match_pattern_raw(cons(spine, nil), [FORMALS("formals")])
    if caps is None:
        raise AssertionError("FORMALS did not match %r" % (spine,))
    return caps["formals"]


class TestGatherFormalsFromMatcher(unittest.TestCase):


    def test_positional_only(self):
        spine = cons(symbol("a"), symbol("b"), nil)
        grp = _match_formals_group(spine)
        pos, defaults, kwonly, star, starstar, ann, by_refs = \
            gather_formals(grp)
        self.assertEqual(pos, [symbol("a"), symbol("b")])
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIsNone(star)
        self.assertIsNone(starstar)
        self.assertEqual(ann, {})

    def test_positional_then_default_keyword(self):
        spine = cons(symbol("a"), cons(keyword("k"), symbol("dflt"), nil))
        grp = _match_formals_group(spine)
        pos, defaults, kwonly, star, starstar, ann, by_refs = \
            gather_formals(grp)
        self.assertEqual(pos, [symbol("a")])
        self.assertEqual(defaults, [(keyword("k"), symbol("dflt"))])
        self.assertEqual(kwonly, [])
        self.assertIsNone(star)
        self.assertIsNone(starstar)

    def test_bracket_default_not_in_positional(self):
        spine = cons(cons(symbol("a"), 1, nil), nil)
        grp = _match_formals_group(spine)
        pos, defaults, kwonly, star, starstar, ann, by_refs = \
            gather_formals(grp)
        self.assertEqual(pos, [])
        self.assertEqual(defaults, [(keyword("a"), 1)])
        self.assertEqual(kwonly, [])
        self.assertIsNone(star)
        self.assertIsNone(starstar)
        self.assertEqual(ann, {})

    def test_star_then_kwonly(self):
        kstar = keyword("*")
        spine = cons(
            symbol("a"),
            kstar,
            symbol("args"),
            symbol("x"),
            keyword("y"),
            symbol("yv"),
            nil)
        grp = _match_formals_group(spine)
        pos, defaults, kwonly, star, starstar, ann, by_refs = \
            gather_formals(grp)
        self.assertEqual(pos, [symbol("a")])
        self.assertEqual(defaults, [])
        self.assertIs(star, symbol("args"))
        self.assertIsNone(starstar)
        self.assertEqual(len(kwonly), 2)
        self.assertEqual(kwonly[0], (keyword("x"), nil))
        self.assertEqual(kwonly[1], (keyword("y"), symbol("yv")))

    def test_starstar_only_tail(self):
        kstar = keyword("*")
        kk = keyword("**")
        spine = cons(kstar, symbol("va"), kk, symbol("kk"), nil)
        grp = _match_formals_group(spine)
        pos, defaults, kwonly, star, starstar, ann, by_refs = \
            gather_formals(grp)
        self.assertEqual(pos, [])
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIs(star, symbol("va"))
        self.assertIs(starstar, symbol("kk"))

    def test_invalid_positional_frame_type_raises(self):
        grp = {"positionals": (42,)}
        with self.assertRaises(SpecialError):
            gather_formals(grp, source=nil)

    def test_star_not_symbol_raises(self):
        grp = {
            "positionals": ({"name": Datum(symbol("x"))},),
            "star": Datum(99),
        }
        with self.assertRaises(SpecialError) as ctx:
            gather_formals(grp, source=cons(symbol("x"), nil))
        self.assertIn("*", str(ctx.exception))

    def test_positional_frame_missing_name_raises(self):
        grp = {
            "positionals": ({"type": Datum(symbol("t"))},),
        }
        with self.assertRaises(SpecialError) as ctx:
            gather_formals(grp, source=nil)
        self.assertIn("name", str(ctx.exception).lower())

    def test_byref_positional_amp_symbol(self):
        # reader-disabled shape: a literal &foo symbol in positional position
        spine = cons(symbol("&foo"), symbol("a"), nil)
        grp = _match_formals_group(spine)
        pos, defaults, kwonly, star, starstar, ann, by_refs = \
            gather_formals(grp)
        self.assertEqual(pos, [symbol("foo"), symbol("a")])
        self.assertEqual(by_refs, ("foo",))

    def test_byref_positional_compile_ref(self):
        # reader-on shape: &foo expands to (compile/ref foo)
        spine = cons(cons(symbol("compile/ref"), symbol("foo"), nil),
                     symbol("a"), nil)
        grp = _match_formals_group(spine)
        pos, defaults, kwonly, star, starstar, ann, by_refs = \
            gather_formals(grp)
        self.assertEqual(pos, [symbol("foo"), symbol("a")])
        self.assertEqual(by_refs, ("foo",))

    def test_byref_positional_ref_alias(self):
        # (ref foo) is identical to (compile/ref foo)
        spine = cons(cons(symbol("ref"), symbol("foo"), nil), nil)
        grp = _match_formals_group(spine)
        pos, defaults, kwonly, star, starstar, ann, by_refs = \
            gather_formals(grp)
        self.assertEqual(pos, [symbol("foo")])
        self.assertEqual(by_refs, ("foo",))

    def test_byref_keyword_default(self):
        # terse keyword by-ref: &acc: dflt reads as keyword '&acc' + value
        spine = cons(symbol("a"),
                     cons(keyword("&acc"), symbol("dflt"), nil))
        grp = _match_formals_group(spine)
        pos, defaults, kwonly, star, starstar, ann, by_refs = \
            gather_formals(grp)
        self.assertEqual(pos, [symbol("a")])
        self.assertEqual(defaults, [(keyword("acc"), symbol("dflt"))])
        self.assertEqual(by_refs, ("acc",))

    def test_byref_kwonly_after_star(self):
        # by-ref keyword-only formal after *: keyword '&acc' + value
        spine = cons(
            keyword("*"), symbol("args"),
            keyword("&acc"), symbol("dflt"),
            nil)
        grp = _match_formals_group(spine)
        pos, defaults, kwonly, star, starstar, ann, by_refs = \
            gather_formals(grp)
        self.assertIs(star, symbol("args"))
        self.assertEqual(kwonly, [(keyword("acc"), symbol("dflt"))])
        self.assertEqual(by_refs, ("acc",))

    def test_byref_bracket_attribute(self):
        # explicit bracket: [out byref: True]
        spine = cons(cons(symbol("out"), keyword("byref"), True, nil), nil)
        grp = _match_formals_group(spine)
        pos, defaults, kwonly, star, starstar, ann, by_refs = \
            gather_formals(grp)
        self.assertEqual(pos, [symbol("out")])
        self.assertEqual(by_refs, ("out",))

    def test_no_byref_is_empty_tuple(self):
        # plain formals report no by-ref names
        spine = cons(symbol("a"), symbol("b"), nil)
        grp = _match_formals_group(spine)
        *_rest, by_refs = gather_formals(grp)
        self.assertEqual(by_refs, ())


# The end.
