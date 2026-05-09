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
Tests for matcher_ng composite matchers built from primitives only.
DEFINE, DECL, BINDING, BINDINGS, FORMALS, USING, and BODY lower into
trees of base nodes and register freeze-time hygiene owners only;
captures are produced by the native engine and asserted directly
"""


import unittest

import pytest

from koskari.lib import cons, keyword, nil, symbol
from koskari.matcher_ng import (
    ABSENT,
    BINDING,
    BINDINGS,
    BODY,
    CAPTURE,
    DECL,
    DEFINE,
    FORMALS,
    GROUP,
    Hygiene,
    HygienicMatchCompiler,
    LITERAL,
    MatchCompiler,
    Matcher,
    MatchResult,
    TYPE,
    USING,
    make_matcher,
)


pytestmark = pytest.mark.phase_infra


SYMBOL_TYPE = type(symbol("x"))
KEYWORD_TYPE = type(keyword("x"))


def _list(*items):
    """
    Build a koskari proper list from items
    """

    if not items:
        return nil
    return cons(*items, nil)


class TestDEFINE(unittest.TestCase):


    def test_define_captures_symbol(self):
        """
        DEFINE binds the named slot to a single symbol element
        """

        pat = GROUP(DEFINE("name"))
        m = pat.build()
        sa = symbol("foo")
        result = m.match((sa,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        self.assertIs(result.captures["name"], sa)


    def test_define_rejects_non_symbol(self):
        """
        DEFINE rejects non-symbol elements at the native engine level
        """

        pat = GROUP(DEFINE("name"))
        m = pat.build()
        self.assertFalse(m.match((42,), symbol=SYMBOL_TYPE).matched)


    def test_define_returns_native_matcher(self):
        """
        Building a DEFINE-bearing pattern yields a native Matcher
        """

        m = GROUP(DEFINE("name")).build()
        self.assertIsInstance(m, Matcher)


    def test_define_registers_binder_meta(self):
        """
        DEFINE contributes a binder hygiene owner that yields the slot symbol
        """

        hc = HygienicMatchCompiler()
        _frozen = hc.lower(GROUP(DEFINE("name")))
        hygiene = hc.build_hygiene()
        binders = hygiene.binders
        self.assertIn("name", binders)
        owner = binders["name"]
        sn = symbol("foo")
        self.assertEqual(set(owner.iter_introduced_symbols({"name": sn})), {sn})
        self.assertEqual(list(owner.iter_recurse_targets({})), [])


class TestDECL(unittest.TestCase):


    def test_decl_bare_symbol(self):
        """
        DECL accepts a bare symbol element
        """

        pat = GROUP(DECL("d"))
        m = pat.build()
        sa = symbol("foo")
        result = m.match((sa,), symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE)
        self.assertTrue(result.matched)
        self.assertIs(result.captures["d"], sa)


    def test_decl_one_element_list(self):
        """
        DECL accepts (name) and binds the inner symbol to 'name'
        """

        pat = GROUP(DECL("d"))
        m = pat.build()
        sa = symbol("foo")
        subj = (_list(sa),)
        result = m.match(subj, symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE)
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["d"], _list(sa))
        self.assertIs(result.captures["name"], sa)


    def test_decl_as_form(self):
        """
        DECL accepts (type as: name) and binds 'type' and 'name'
        """

        pat = GROUP(DECL("d"))
        m = pat.build()
        sa = symbol("Foo")
        sb = symbol("bar")
        subj = (_list(sa, keyword("as"), sb),)
        result = m.match(subj, symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE)
        self.assertTrue(result.matched)
        self.assertIs(result.captures["type"], sa)
        self.assertIs(result.captures["name"], sb)


    def test_decl_name_with_keyword_tail(self):
        """
        DECL accepts (name kw1 v1 kw2 v2) and exposes the raw tail as
        decl_tail so downstream consumers walk the keyword pairs
        """

        pat = GROUP(DECL("d"))
        m = pat.build()
        sa = symbol("foo")
        kt = keyword("type")
        st = symbol("Bar")
        tail_form = _list(sa, kt, st)
        result = m.match(
            (tail_form,),
            symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE)
        self.assertTrue(result.matched)
        self.assertIs(result.captures["name"], sa)
        self.assertEqual(result.captures["decl_tail"], cons(kt, st, nil))


    def test_decl_rejects_non_symbol_non_pair(self):
        """
        DECL rejects elements that are neither a symbol nor a proper
        pair such as a literal int
        """

        pat = GROUP(DECL("d"))
        m = pat.build()
        result = m.match((42,), symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE)
        self.assertFalse(result.matched)


    def test_decl_registers_binder_meta(self):
        """
        DECL binder owner extracts introduced symbols from name or bare slot
        """

        hc = HygienicMatchCompiler()
        _frozen = hc.lower(GROUP(DECL("d")))
        hygiene = hc.build_hygiene()
        owner = hygiene.binders["d"]
        sx = symbol("x")
        self.assertEqual(set(owner.iter_introduced_symbols({"name": sx})), {sx})


class TestBINDING(unittest.TestCase):


    def test_binding_name_expr_form(self):
        """
        BINDING captures (name expr) into the named slot and exposes
        the inner name and expr captures
        """

        pat = GROUP(BINDING("b"))
        m = pat.build()
        sa = symbol("foo")
        subj = (_list(sa, 7),)
        result = m.match(subj, symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE)
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["b"], _list(sa, 7))
        self.assertIs(result.captures["name"], sa)
        self.assertEqual(result.captures["expr"], 7)


    def test_binding_expr_as_name_form(self):
        """
        BINDING captures (expr as: name) and exposes both inner slots
        """

        pat = GROUP(BINDING("b"))
        m = pat.build()
        sa = symbol("foo")
        subj = (_list(7, keyword("as"), sa),)
        result = m.match(subj, symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE)
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["expr"], 7)
        self.assertIs(result.captures["name"], sa)


    def test_binding_rejects_unrecognised_shape(self):
        """
        BINDING rejects forms that match neither inner shape
        """

        pat = GROUP(BINDING("b"))
        m = pat.build()
        sa = symbol("foo")
        subj = (_list(sa, 1, 2),)
        result = m.match(subj, symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE)
        self.assertFalse(result.matched)


    def test_binding_registers_binder_meta(self):
        """
        BINDING binder owner yields name and expr recurse targets
        """

        hc = HygienicMatchCompiler()
        _frozen = hc.lower(GROUP(BINDING("b")))
        hygiene = hc.build_hygiene()
        owner = hygiene.binders["b"]
        sn = symbol("n")
        self.assertEqual(set(owner.iter_introduced_symbols({"name": sn})), {sn})
        self.assertEqual(list(owner.iter_recurse_targets({"expr": 99})), [99])


class TestBINDINGS(unittest.TestCase):


    def test_bindings_inline_yields_frame_list(self):
        """
        BINDINGS consumes inline binding cells from the surrounding
        spine and produces a list of per-cell frame dicts holding
        name and expr. The remainder of the subject is consumed by
        the trailing REST capture
        """

        pat = GROUP(BINDINGS("bs"), CAPTURE("tail"))
        m = pat.build()
        sa = symbol("a")
        sb = symbol("b")
        subj = (_list(sa, 1), _list(sb, 2), "tail")
        result = m.match(subj, symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE)
        self.assertTrue(result.matched)
        frames = result.captures["bs"]
        self.assertIsInstance(frames, list)
        self.assertEqual(len(frames), 2)
        self.assertIs(frames[0]["name"], sa)
        self.assertEqual(frames[0]["expr"], 1)
        self.assertIs(frames[1]["name"], sb)
        self.assertEqual(frames[1]["expr"], 2)
        self.assertEqual(result.captures["tail"], "tail")


    def test_bindings_empty_marker_nil(self):
        """
        BINDINGS accepts a single nil sentinel element and binds the
        slot to nil itself
        """

        pat = GROUP(BINDINGS("bs"), CAPTURE("tail"))
        m = pat.build()
        result = m.match(
            (nil, "tail"), symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE)
        self.assertTrue(result.matched)
        self.assertIs(result.captures["bs"], nil)
        self.assertEqual(result.captures["tail"], "tail")


    def test_bindings_zero_inline_cells(self):
        """
        BINDINGS adjacent to a non-binding-shaped element consumes
        zero cells and binds the slot to an empty list
        """

        pat = GROUP(BINDINGS("bs"), CAPTURE("tail"))
        m = pat.build()
        result = m.match(
            (42,), symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE)
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["bs"], [])
        self.assertEqual(result.captures["tail"], 42)


    def test_bindings_default_mode_recorded(self):
        """
        BINDINGS default mode stays on the composite instance
        """

        bs = BINDINGS("bs")
        hc = HygienicMatchCompiler()
        _frozen = hc.lower(GROUP(bs))
        hygiene = hc.build_hygiene()
        self.assertEqual(bs._mode, "parallel")
        self.assertIs(hygiene.binders["bs"], bs)


    def test_bindings_sequential_mode_recorded(self):
        """
        BINDINGS preserves an explicit mode argument on the composite instance
        """

        bs = BINDINGS("bs", mode="sequential")
        hc = HygienicMatchCompiler()
        _frozen = hc.lower(GROUP(bs))
        hygiene = hc.build_hygiene()
        self.assertEqual(bs._mode, "sequential")
        self.assertIs(hygiene.binders["bs"], bs)


class TestUSING(unittest.TestCase):


    def test_using_name_expr(self):
        """
        USING accepts (name expr) and binds name and expr; the
        unpack-only slot lhs remains ABSENT
        """

        pat = GROUP(USING("u"))
        m = pat.build()
        sa = symbol("foo")
        subj = (_list(sa, 7),)
        result = m.match(
            subj,
            symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE,
            pair=type(cons(1, nil)))
        self.assertTrue(result.matched)
        self.assertIs(result.captures["name"], sa)
        self.assertEqual(result.captures["expr"], 7)
        self.assertIs(result.captures["lhs"], ABSENT)


    def test_using_expr_as_name(self):
        """
        USING accepts (expr as: name) and binds expr and name; lhs
        remains ABSENT
        """

        pat = GROUP(USING("u"))
        m = pat.build()
        sa = symbol("foo")
        subj = (_list(7, keyword("as"), sa),)
        result = m.match(
            subj,
            symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE,
            pair=type(cons(1, nil)))
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["expr"], 7)
        self.assertIs(result.captures["name"], sa)
        self.assertIs(result.captures["lhs"], ABSENT)


    def test_using_bare_expr(self):
        """
        USING accepts (expr) and binds expr only
        """

        pat = GROUP(USING("u"))
        m = pat.build()
        subj = (_list(7),)
        result = m.match(
            subj,
            symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE,
            pair=type(cons(1, nil)))
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["expr"], 7)
        self.assertIs(result.captures["name"], ABSENT)
        self.assertIs(result.captures["lhs"], ABSENT)


    def test_using_unpack_form(self):
        """
        USING accepts ((pat...) expr) and binds the lhs pair plus expr
        """

        pat = GROUP(USING("u"))
        m = pat.build()
        sa = symbol("a")
        sb = symbol("b")
        lhs = _list(sa, sb)
        subj = (_list(lhs, 7),)
        result = m.match(
            subj,
            symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE,
            pair=type(cons(1, nil)))
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["lhs"], lhs)
        self.assertEqual(result.captures["expr"], 7)
        self.assertIs(result.captures["name"], ABSENT)


    def test_using_rejects_non_pair(self):
        """
        USING rejects a non-pair element: the FORM descent fails
        """

        pat = GROUP(USING("u"))
        m = pat.build()
        result = m.match(
            (42,),
            symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE,
            pair=type(cons(1, nil)))
        self.assertFalse(result.matched)


    def test_using_registers_binder_meta(self):
        """
        USING binder owner extracts optional name and expr recurse
        """

        hc = HygienicMatchCompiler()
        _frozen = hc.lower(GROUP(USING("u")))
        hygiene = hc.build_hygiene()
        owner = hygiene.binders["u"]
        sn = symbol("n")
        self.assertEqual(set(owner.iter_introduced_symbols({"name": sn})), {sn})
        self.assertEqual(list(owner.iter_recurse_targets({"expr": 1})), [1])


class TestFORMALS(unittest.TestCase):


    def _externs(self):
        """
        Common extern bindings used by FORMALS match calls
        """

        return {
            "symbol": SYMBOL_TYPE,
            "keyword": KEYWORD_TYPE,
        }


    def test_formals_bare_symbol(self):
        """
        FORMALS accepts a bare symbol formals shape (the entire formals
        list is one symbol). Inner phase slots remain ABSENT
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        sa = symbol("rest")
        result = m.match((sa,), **self._externs())
        self.assertTrue(result.matched)
        self.assertIs(result.captures["f"], sa)
        self.assertIs(result.captures["positionals"], ABSENT)
        self.assertIs(result.captures["defaults"], ABSENT)
        self.assertIs(result.captures["star"], ABSENT)
        self.assertIs(result.captures["kwonly"], ABSENT)
        self.assertIs(result.captures["starstar"], ABSENT)


    def test_formals_empty_list(self):
        """
        FORMALS accepts the empty proper list (). All inner REPEAT
        phases produce empty frame lists; non-REPEAT segments remain
        ABSENT
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        result = m.match((nil,), **self._externs())
        self.assertTrue(result.matched)
        self.assertIs(result.captures["f"], nil)
        self.assertEqual(result.captures["positionals"], [])
        self.assertEqual(result.captures["defaults"], [])
        self.assertEqual(result.captures["kwonly"], [])
        self.assertIs(result.captures["star"], ABSENT)
        self.assertIs(result.captures["starstar"], ABSENT)


    def test_formals_simple_positionals(self):
        """
        FORMALS accepts (a b c) and produces three positional frames
        each holding 'name'
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        sb = symbol("b")
        sc = symbol("c")
        formals = _list(sa, sb, sc)
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["f"], formals)
        positionals = result.captures["positionals"]
        self.assertIsInstance(positionals, list)
        self.assertEqual(len(positionals), 3)
        self.assertIs(positionals[0]["name"], sa)
        self.assertIs(positionals[1]["name"], sb)
        self.assertIs(positionals[2]["name"], sc)


    def test_formals_single_positional(self):
        """
        FORMALS accepts a single-element positional list (self) and
        produces one positional frame
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        sself = symbol("self")
        formals = _list(sself)
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        positionals = result.captures["positionals"]
        self.assertEqual(len(positionals), 1)
        self.assertIs(positionals[0]["name"], sself)


    def test_formals_positionals_with_one_default(self):
        """
        FORMALS accepts (self threshold: 0.6) -- a typical method
        shape with one positional and one keyword default
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        sself = symbol("self")
        kthresh = keyword("threshold")
        formals = _list(sself, kthresh, 0.6)
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        positionals = result.captures["positionals"]
        self.assertEqual(len(positionals), 1)
        self.assertIs(positionals[0]["name"], sself)
        defaults = result.captures["defaults"]
        self.assertEqual(len(defaults), 1)
        self.assertIs(defaults[0]["kw"], kthresh)
        self.assertEqual(defaults[0]["default"], 0.6)


    def test_formals_pure_defaults_only(self):
        """
        FORMALS accepts (alive: "O" dead: " "), pure keyword/value
        defaults with no positional preamble
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        kalive = keyword("alive")
        kdead = keyword("dead")
        formals = _list(kalive, "O", kdead, " ")
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["positionals"], [])
        defaults = result.captures["defaults"]
        self.assertEqual(len(defaults), 2)
        self.assertIs(defaults[0]["kw"], kalive)
        self.assertEqual(defaults[0]["default"], "O")
        self.assertIs(defaults[1]["kw"], kdead)
        self.assertEqual(defaults[1]["default"], " ")


    def test_formals_single_default_only(self):
        """
        FORMALS accepts (argv: None), a single keyword/default with
        no other phases
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        kargv = keyword("argv")
        formals = _list(kargv, None)
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        defaults = result.captures["defaults"]
        self.assertEqual(len(defaults), 1)
        self.assertIs(defaults[0]["kw"], kargv)
        self.assertIsNone(defaults[0]["default"])


    def test_formals_only_star(self):
        """
        FORMALS accepts (*: rest) with no positional preamble
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        rest = symbol("rest")
        formals = _list(keyword("*"), rest)
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["positionals"], [])
        self.assertIs(result.captures["star"], rest)
        self.assertIs(result.captures["starstar"], ABSENT)


    def test_formals_only_starstar(self):
        """
        FORMALS accepts (**: kw) with no positional preamble
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        skw = symbol("kw")
        formals = _list(keyword("**"), skw)
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["positionals"], [])
        self.assertIs(result.captures["starstar"], skw)
        self.assertIs(result.captures["star"], ABSENT)


    def test_formals_bracket_name_only(self):
        """
        FORMALS accepts a bracket positional with just a symbol such
        as ((x)); the frame holds 'name' and no other inner slots
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        sx = symbol("x")
        formals = _list(_list(sx))
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        positionals = result.captures["positionals"]
        self.assertEqual(len(positionals), 1)
        self.assertIs(positionals[0]["name"], sx)


    def test_formals_bracket_keyword_attributes(self):
        """
        FORMALS accepts a bracket positional with keyword attributes
        such as ((x :: T)); the frame holds 'name' and an 'attrs'
        list of keyword/value frames
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        sx = symbol("x")
        st = symbol("T")
        formals = _list(_list(sx, keyword("::"), st))
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        positionals = result.captures["positionals"]
        self.assertEqual(len(positionals), 1)
        self.assertIs(positionals[0]["name"], sx)
        attrs = positionals[0]["attrs"]
        self.assertIsInstance(attrs, list)
        self.assertEqual(len(attrs), 1)
        self.assertEqual(attrs[0]["attr_val"], st)


    def test_formals_rejects_improper_list(self):
        """
        FORMALS rejects an improper-list formals shape such as
        (a b . c); improper formals lists are illegal in koskari
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        sb = symbol("b")
        sc = symbol("c")
        improper = cons(sa, cons(sb, sc))
        result = m.match((improper,), **self._externs())
        self.assertFalse(result.matched)


    def test_formals_rejects_non_symbol_non_list(self):
        """
        FORMALS rejects an element that is neither a symbol nor a
        proper list, such as a number
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        result = m.match((42,), **self._externs())
        self.assertFalse(result.matched)


    def test_formals_star_segment(self):
        """
        FORMALS accepts (a *: rest) and binds 'star' to the rest symbol
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        rest = symbol("rest")
        formals = _list(sa, keyword("*"), rest)
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        self.assertEqual(len(result.captures["positionals"]), 1)
        self.assertIs(result.captures["positionals"][0]["name"], sa)
        self.assertIs(result.captures["star"], rest)
        self.assertIs(result.captures["starstar"], ABSENT)


    def test_formals_starstar_segment(self):
        """
        FORMALS accepts (a **: kw) and binds 'starstar' to the kwargs
        symbol
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        kw = symbol("kw")
        formals = _list(sa, keyword("**"), kw)
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        self.assertIs(result.captures["starstar"], kw)
        self.assertIs(result.captures["star"], ABSENT)


    def test_formals_default_keyword_value_pairs(self):
        """
        FORMALS accepts (a foo: 1 bar: 2) and produces two default
        frames each holding 'kw' (a keyword) and 'default' (a value)
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        foo = keyword("foo")
        bar = keyword("bar")
        formals = _list(sa, foo, 1, bar, 2)
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        defaults = result.captures["defaults"]
        self.assertIsInstance(defaults, list)
        self.assertEqual(len(defaults), 2)
        self.assertIs(defaults[0]["kw"], foo)
        self.assertEqual(defaults[0]["default"], 1)
        self.assertIs(defaults[1]["kw"], bar)
        self.assertEqual(defaults[1]["default"], 2)


    def test_formals_star_then_kwonly(self):
        """
        FORMALS accepts (a *: rest x y: 2 **: kw) and decomposes into
        positional, star, kw-only, starstar phases
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        rest = symbol("rest")
        sx = symbol("x")
        ky = keyword("y")
        skw = symbol("kw")
        formals = _list(
            sa, keyword("*"), rest, sx, ky, 2, keyword("**"), skw)
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        self.assertIs(result.captures["star"], rest)
        kwonly = result.captures["kwonly"]
        self.assertIsInstance(kwonly, list)
        self.assertEqual(len(kwonly), 2)
        self.assertIs(kwonly[0]["name"], sx)
        self.assertIs(kwonly[1]["kw"], ky)
        self.assertEqual(kwonly[1]["default"], 2)
        self.assertIs(result.captures["starstar"], skw)


    def test_formals_star_nil_marker(self):
        """
        FORMALS accepts (a *: ()) where (the keyword is followed by
        nil); the star slot binds to nil
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        formals = _list(sa, keyword("*"), nil)
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        self.assertIs(result.captures["star"], nil)


    def test_formals_bracket_default(self):
        """
        FORMALS accepts a bracket positional with default such as
        (a (b 1)) and the bracket frame holds name and default
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        sb = symbol("b")
        formals = _list(sa, _list(sb, 1))
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        positionals = result.captures["positionals"]
        self.assertEqual(len(positionals), 2)
        self.assertIs(positionals[0]["name"], sa)
        self.assertIs(positionals[1]["name"], sb)
        self.assertEqual(positionals[1]["default"], 1)


    def test_formals_bracket_as_form(self):
        """
        FORMALS accepts a bracket positional (T as: name) and the
        frame holds 'type' and 'name'
        """

        pat = GROUP(FORMALS("f"))
        m = pat.build()
        st = symbol("T")
        sn = symbol("n")
        formals = _list(_list(st, keyword("as"), sn))
        result = m.match((formals,), **self._externs())
        self.assertTrue(result.matched)
        positionals = result.captures["positionals"]
        self.assertEqual(len(positionals), 1)
        self.assertIs(positionals[0]["type"], st)
        self.assertIs(positionals[0]["name"], sn)


    def test_formals_registers_binder_meta(self):
        """
        FORMALS binder owner extracts bare-symbol formals as introduced names
        """

        hc = HygienicMatchCompiler()
        _frozen = hc.lower(GROUP(FORMALS("f")))
        hygiene = hc.build_hygiene()
        owner = hygiene.binders["f"]
        sf = symbol("rest")
        self.assertEqual(set(owner.iter_introduced_symbols({"f": sf})), {sf})


class TestBODY(unittest.TestCase):


    def test_body_captures_rest_like_rest(self):
        """
        BODY captures the remainder of the source list into the named
        slot, identical to REST at the engine level
        """

        pat = GROUP(CAPTURE("head"), BODY("body"))
        m = pat.build()
        result = m.match((1, 2, 3))
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["head"], 1)
        self.assertEqual(result.captures["body"], (2, 3))


    def test_body_registers_scope_region(self):
        """
        BODY registers a scope-region owner; iter_scope_targets yields the
        capture once (the tuple tail as a single lexical root)
        """

        pat = GROUP(CAPTURE("head"), BODY("body"))
        hc = HygienicMatchCompiler()
        _frozen = hc.lower(pat)
        hygiene = hc.build_hygiene()
        regions = hygiene.scope_regions
        self.assertIn("body", regions)
        body_owner = regions["body"]
        bsym = symbol("b")
        roots = list(body_owner.iter_scope_targets({"body": (bsym,)}))
        self.assertEqual(len(roots), 1)
        self.assertEqual(roots[0], (bsym,))


class TestRuntimeCompileHygiene(unittest.TestCase):


    def test_primitives_compile_without_source_hygiene(self):
        """
        Pure primitive trees lowered with MatchCompiler emit no hygiene
        plan
        """

        pat = GROUP(CAPTURE("x", TYPE("symbol")))
        hc = HygienicMatchCompiler()
        _frozen = hc.lower(pat)
        self.assertTrue(hc.build_hygiene().is_empty())


    def test_body_compile_ignores_hygiene_when_not_hygienic(self):
        """
        BODY registers scope only under HygienicMatchCompiler; baseline
        lower leaves the hygiene plan empty
        """

        pat = GROUP(BODY("b"))
        _frozen = MatchCompiler().lower(pat)
        m = Matcher.from_tuple(_frozen)
        result = m.match((1, 2, 3))
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["b"], (1, 2, 3))


    def test_define_compile_ignores_hygiene_when_not_hygienic(self):
        """
        DEFINE registers binders only under HygienicMatchCompiler
        """

        pat = GROUP(DEFINE("n"))
        _frozen = MatchCompiler().lower(pat)
        m = Matcher.from_tuple(_frozen)
        sa = symbol("x")
        result = m.match((sa,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        self.assertIs(result.captures["n"], sa)


class TestComposition(unittest.TestCase):


    def test_define_with_body(self):
        """
        A define-shaped pattern composes DEFINE with a BODY scope
        region
        """

        pat = GROUP(DEFINE("name"), BODY("body"))
        m = pat.build()
        sa = symbol("foo")
        result = m.match((sa, 1, 2, 3), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        self.assertIs(result.captures["name"], sa)
        self.assertEqual(result.captures["body"], (1, 2, 3))


    def test_define_with_formals_and_body(self):
        """
        A define-with-formals pattern composes DEFINE, FORMALS, and
        BODY into one matcher
        """

        pat = GROUP(DEFINE("name"), FORMALS("formals"), BODY("body"))
        m = pat.build()
        sa = symbol("foo")
        sx = symbol("x")
        sy = symbol("y")
        formals = _list(sx, sy)
        result = m.match(
            (sa, formals, "b1", "b2"),
            symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE)
        self.assertTrue(result.matched)
        self.assertIs(result.captures["name"], sa)
        self.assertEqual(result.captures["formals"], formals)
        self.assertEqual(result.captures["body"], ("b1", "b2"))
        positionals = result.captures["positionals"]
        self.assertEqual(len(positionals), 2)


    def test_let_shaped_pattern_with_bindings_and_body(self):
        """
        A let-shaped pattern composes BINDINGS with BODY into one
        matcher; binding cells are inlined into the spine ahead of
        the body
        """

        pat = GROUP(BINDINGS("bs"), BODY("body"))
        m = pat.build()
        sa = symbol("a")
        sb = symbol("b")
        result = m.match(
            (_list(sa, 1), _list(sb, 2), "b1", "b2"),
            symbol=SYMBOL_TYPE, keyword=KEYWORD_TYPE)
        self.assertTrue(result.matched)
        frames = result.captures["bs"]
        self.assertEqual(len(frames), 2)
        self.assertEqual(result.captures["body"], ("b1", "b2"))


class TestHygieneCompile(unittest.TestCase):


    def test_compile_exposes_binders_and_scope_regions(self):
        """
        Composite-bearing patterns produce a Hygiene plan with binders and
        scope_regions sections
        """

        dn = DEFINE("name")
        bod = BODY("body")
        pat = GROUP(dn, bod)
        hc = HygienicMatchCompiler()
        _frozen = hc.lower(pat)
        hygiene = hc.build_hygiene()
        self.assertIsInstance(hygiene, Hygiene)
        self.assertIs(hygiene.binders["name"], dn)
        self.assertIs(hygiene.scope_regions["body"], bod)


class TestMakeMatcher(unittest.TestCase):


    def test_make_matcher_returns_native_matcher(self):
        """
        make_matcher returns a native Matcher and never a wrapper
        """

        pat = GROUP(DEFINE("name"))
        m = make_matcher(pat)
        self.assertIsInstance(m, Matcher)


    def test_make_matcher_returns_native_match_result(self):
        """
        make_matcher's matcher returns native MatchResult instances
        """

        pat = GROUP(DEFINE("name"))
        m = make_matcher(pat)
        sa = symbol("foo")
        result = m.match((sa,), symbol=SYMBOL_TYPE)
        self.assertIsInstance(result, MatchResult)


# The end.
