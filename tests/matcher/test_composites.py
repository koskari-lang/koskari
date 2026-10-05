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
Tests for matcher composite matchers built from primitives only.
DEFINE, DECL, BINDING, BINDINGS, FORMALS, USING, and BODY lower into
trees of base nodes; captures are produced by the native engine and
asserted directly
"""


import collections.abc
import unittest

import pytest

from koskari.lib import (
    Datum, cdr, cons, keyword, nil, setcar, setcdr, symbol)
from koskari.matcher import (
    BINDING, BINDINGS, BODY, CAPTURE, DECL, DEFINE, FORM, FORMALS, LITERAL,
    MATCH, PREDICATE,
    REST, TYPE, USING, VALUE, DatumFrame,
    MatchCompiler, Matcher, MatchResult, make_matcher)
from koskari.types import build_proper


pytestmark = pytest.mark.phase_infra


def _peel_frame_row(item):
    """
    One REPEAT frame dict: use .datum on each value; peel nested frame
    lists (e.g. attrs) the same way
    """

    if isinstance(item, dict):
        out = {}
        for kk, vv in item.items():
            if isinstance(vv, Datum):
                d = vv.datum
            else:
                d = vv
            if isinstance(d, list):
                out[kk] = [_peel_frame_row(x) for x in d]
            else:
                out[kk] = d
        return out
    if isinstance(item, Datum):
        return item.datum
    return item


def _peel_capture_value(v):
    """
    Top-level or GROUP-nested capture: dict recurse; Datum peel; list of
    frames via _peel_frame_row
    """

    if isinstance(v, dict):
        return {
            str(kk): _peel_capture_value(vv)
            for kk, vv in v.items()}
    if isinstance(v, Datum):
        d = v.datum
    else:
        d = v
    if isinstance(d, list):
        return [_peel_frame_row(item) for item in d]
    return d


def _ucaps(result):
    """
    Copy captures for assertions: .datum on each top-level slot; GROUP dicts
    recurse; list values peel each frame dict entry
    """

    out = {}
    for k, v in result.captures.items():
        out[str(k)] = _peel_capture_value(v)
    return out


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

        pat = MATCH(DEFINE("name"))
        m = pat.build()
        sa = symbol("foo")
        result = m.match((sa,))
        self.assertTrue(result.matched)
        self.assertIs(_ucaps(result)["name"], sa)


    def test_define_rejects_non_symbol(self):
        """
        DEFINE rejects non-symbol elements at the native engine level
        """

        pat = MATCH(DEFINE("name"))
        m = pat.build()
        self.assertFalse(m.match((42,)).matched)


    def test_define_returns_native_matcher(self):
        """
        Building a DEFINE-bearing pattern yields a native Matcher
        """

        m = MATCH(DEFINE("name")).build()
        self.assertIsInstance(m, Matcher)


class TestDECL(unittest.TestCase):


    def test_decl_bare_symbol(self):
        """
        DECL accepts a bare symbol element
        """

        pat = MATCH(DECL("d"))
        m = pat.build()
        sa = symbol("foo")
        result = m.match(build_proper(sa))
        self.assertTrue(result.matched)
        d = result.captures["d"]
        self.assertIsInstance(d, DatumFrame)
        self.assertEqual(d.source, build_proper(sa))


    def test_decl_one_element_list(self):
        """
        DECL accepts (name) and binds the inner symbol to 'name'
        """

        pat = MATCH(DECL("d"))
        m = pat.build()
        sa = symbol("foo")
        inner = _list(sa)
        result = m.match(build_proper(inner))
        self.assertTrue(result.matched)
        d = result.captures["d"]
        self.assertIsInstance(d, DatumFrame)
        self.assertEqual(d.source, build_proper(inner))
        self.assertIs(_ucaps(result)["d"]["name"], sa)


    def test_decl_as_form(self):
        """
        DECL accepts (type as: name) and binds 'type' and 'name'
        """

        pat = MATCH(DECL("d"))
        m = pat.build()
        sa = symbol("Foo")
        sb = symbol("bar")
        subj = (_list(sa, keyword("as"), sb),)
        result = m.match(subj)
        self.assertTrue(result.matched)
        self.assertIs(_ucaps(result)["d"]["type"], sa)
        self.assertIs(_ucaps(result)["d"]["name"], sb)


    def test_decl_name_with_keyword_tail(self):
        """
        DECL accepts (name kw1 v1 kw2 v2) and exposes the raw tail as
        decl_tail so downstream consumers walk the keyword pairs
        """

        pat = MATCH(DECL("d"))
        m = pat.build()
        sa = symbol("foo")
        kt = keyword("type")
        st = symbol("Bar")
        tail_form = _list(sa, kt, st)
        result = m.match((tail_form,))
        self.assertTrue(result.matched)
        self.assertIs(_ucaps(result)["d"]["name"], sa)
        self.assertEqual(_ucaps(result)["d"]["decl_tail"], cons(kt, st, nil))


    def test_decl_rejects_non_symbol_non_pair(self):
        """
        DECL rejects elements that are neither a symbol nor a proper
        pair such as a literal int
        """

        pat = MATCH(DECL("d"))
        m = pat.build()
        result = m.match((42,))
        self.assertFalse(result.matched)


class TestBINDING(unittest.TestCase):


    def test_binding_name_expr_form(self):
        """
        BINDING captures (name expr) into the named slot and exposes
        the inner name and expr captures
        """

        pat = MATCH(BINDING("b"))
        m = pat.build()
        sa = symbol("foo")
        cell = _list(sa, 7)
        result = m.match(build_proper(cell))
        self.assertTrue(result.matched)
        b = result.captures["b"]
        self.assertIsInstance(b, DatumFrame)
        self.assertEqual(b.source, build_proper(cell))
        self.assertIs(_ucaps(result)["b"]["name"], sa)
        self.assertEqual(_ucaps(result)["b"]["expr"], 7)


    def test_binding_expr_as_name_form(self):
        """
        BINDING captures (expr as: name) and exposes both inner slots
        """

        pat = MATCH(BINDING("b"))
        m = pat.build()
        sa = symbol("foo")
        subj = (_list(7, keyword("as"), sa),)
        result = m.match(subj)
        self.assertTrue(result.matched)
        self.assertEqual(_ucaps(result)["b"]["expr"], 7)
        self.assertIs(_ucaps(result)["b"]["name"], sa)


    def test_binding_rejects_unrecognised_shape(self):
        """
        BINDING rejects forms that match neither inner shape
        """

        pat = MATCH(BINDING("b"))
        m = pat.build()
        sa = symbol("foo")
        subj = (_list(sa, 1, 2),)
        result = m.match(subj)
        self.assertFalse(result.matched)


class TestBINDINGS(unittest.TestCase):


    def test_bindings_inline_yields_frame_list(self):
        """
        BINDINGS matches one spine cell: ``nil`` or a single proper list of
        binding-shaped cells. The repeat runs inside that cell, so several
        bindings are one outer list element; the next spine slot is the tail
        capture
        """

        pat = MATCH(BINDINGS("bs"), CAPTURE("tail"))
        m = pat.build()
        sa = symbol("a")
        sb = symbol("b")
        c1 = _list(sa, 1)
        c2 = _list(sb, 2)
        subj = build_proper(_list(c1, c2), "tail")
        result = m.match(subj)
        self.assertTrue(result.matched)
        frames_raw = result.captures["bs"]
        self.assertIsInstance(frames_raw, list)
        self.assertEqual(len(frames_raw), 2)
        self.assertIsInstance(frames_raw[0], DatumFrame)
        self.assertIs(frames_raw[0].source, c1)
        self.assertIsInstance(frames_raw[1], DatumFrame)
        self.assertEqual(frames_raw[1].source, build_proper(c2))
        frames = _ucaps(result)["bs"]
        self.assertIs(frames[0]["name"], sa)
        self.assertEqual(frames[0]["expr"], 1)
        self.assertIs(frames[1]["name"], sb)
        self.assertEqual(frames[1]["expr"], 2)
        self.assertEqual(_ucaps(result)["tail"], "tail")


    def test_bindings_empty_marker_nil(self):
        """
        BINDINGS accepts a single nil sentinel element and binds the
        slot to nil itself
        """

        pat = MATCH(BINDINGS("bs"), CAPTURE("tail"))
        m = pat.build()
        result = m.match(build_proper(nil, "tail"))
        self.assertTrue(result.matched)
        self.assertIs(_ucaps(result)["bs"], nil)
        self.assertEqual(_ucaps(result)["tail"], "tail")


    def test_bindings_rejects_non_binding_list_first_cell(self):
        """
        The binding-list cell must be ``nil`` or a list of binding pairs; a
        non-list first spine element does not match the nonempty arm and is
        not ``nil``, so the whole pattern fails
        """

        pat = MATCH(BINDINGS("bs"), CAPTURE("tail"))
        m = pat.build()
        self.assertFalse(m.match((42, "tail")).matched)


class TestUSING(unittest.TestCase):


    def test_using_name_expr(self):
        """
        USING accepts (name expr) and binds name and expr; the
        unpack-only slot lhs is omitted
        """

        pat = MATCH(USING("u"))
        m = pat.build()
        sa = symbol("foo")
        subj = (_list(sa, 7),)
        result = m.match(subj)
        self.assertTrue(result.matched)
        c = _ucaps(result)
        self.assertIs(c["u"]["name"], sa)
        self.assertEqual(c["u"]["expr"], 7)
        self.assertNotIn("lhs", c["u"])


    def test_using_expr_as_name(self):
        """
        USING accepts (expr as: name) and binds expr and name; lhs
        key is omitted
        """

        pat = MATCH(USING("u"))
        m = pat.build()
        sa = symbol("foo")
        subj = (_list(7, keyword("as"), sa),)
        result = m.match(subj)
        self.assertTrue(result.matched)
        c = _ucaps(result)
        self.assertEqual(c["u"]["expr"], 7)
        self.assertIs(c["u"]["name"], sa)
        self.assertNotIn("lhs", c["u"])


    def test_using_bare_expr(self):
        """
        USING accepts (expr) and binds expr only
        """

        pat = MATCH(USING("u"))
        m = pat.build()
        subj = (_list(7),)
        result = m.match(subj)
        self.assertTrue(result.matched)
        c = _ucaps(result)
        self.assertEqual(c["u"]["expr"], 7)
        self.assertNotIn("name", c["u"])
        self.assertNotIn("lhs", c["u"])


    def test_using_unpack_form(self):
        """
        USING accepts ((pat...) expr) and binds the lhs pair plus expr
        """

        pat = MATCH(USING("u"))
        m = pat.build()
        sa = symbol("a")
        sb = symbol("b")
        lhs = _list(sa, sb)
        subj = (_list(lhs, 7),)
        result = m.match(subj)
        self.assertTrue(result.matched)
        c = _ucaps(result)
        self.assertEqual(c["u"]["lhs"], lhs)
        self.assertEqual(c["u"]["expr"], 7)
        self.assertNotIn("name", c["u"])


    def test_using_rejects_non_pair(self):
        """
        USING rejects a non-pair element: the FORM descent fails
        """

        pat = MATCH(USING("u"))
        m = pat.build()
        result = m.match((42,))
        self.assertFalse(result.matched)


class TestFORMALS(unittest.TestCase):

    def test_formals_rejects_bare_symbol(self):
        """
        matcher FORMALS matches only proper-list formals cells, not a lone
        symbol
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        sa = symbol("rest")
        result = m.match((sa,))
        self.assertFalse(result.matched)


    def test_formals_empty_list(self):
        """
        FORMALS accepts the empty proper list (). All inner REPEAT
        phases produce empty frame lists; non-REPEAT segment keys are
        omitted
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        result = m.match(build_proper(nil))
        self.assertTrue(result.matched)
        f = result.captures["f"]
        self.assertIsInstance(f, DatumFrame)
        self.assertEqual(f.source, build_proper(nil))
        c = _ucaps(result)
        f = c["f"]
        self.assertEqual(f["positionals"], [])
        self.assertEqual(f["defaults"], [])
        self.assertEqual(f["kwonly"], [])
        self.assertNotIn("star", f)
        self.assertNotIn("starstar", f)


    def test_formals_simple_positionals(self):
        """
        FORMALS accepts (a b c) and produces three positional frames
        each holding 'name'
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        sb = symbol("b")
        sc = symbol("c")
        formals = _list(sa, sb, sc)
        result = m.match(build_proper(formals))
        self.assertTrue(result.matched)
        fr = result.captures["f"]
        self.assertIsInstance(fr, DatumFrame)
        self.assertEqual(fr.source, build_proper(formals))
        u = _ucaps(result)
        positionals = u["f"]["positionals"]
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

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        sself = symbol("self")
        formals = _list(sself)
        result = m.match((formals,))
        self.assertTrue(result.matched)
        positionals = _ucaps(result)["f"]["positionals"]
        self.assertEqual(len(positionals), 1)
        self.assertIs(positionals[0]["name"], sself)


    def test_formals_keyword_headed_positional_cell(self):
        """
        FORMALS accepts ((x :: int) (y: 1 :: int)); the keyword-headed
        cell yields a positional frame holding 'kw', 'default', and
        an attrs frame for the annotation
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        sx = symbol("x")
        ky = keyword("y")
        kann = keyword("")
        sint = symbol("int")
        formals = _list(_list(sx, kann, sint), _list(ky, 1, kann, sint))
        result = m.match((formals,))
        self.assertTrue(result.matched)
        u = _ucaps(result)
        positionals = u["f"]["positionals"]
        self.assertEqual(len(positionals), 2)
        self.assertIs(positionals[0]["name"], sx)
        self.assertIs(positionals[1]["kw"], ky)
        self.assertEqual(positionals[1]["default"], 1)
        self.assertNotIn("name", positionals[1])
        attrs = positionals[1]["attrs"]
        self.assertEqual(len(attrs), 1)
        self.assertIs(attrs[0]["kw"], kann)
        self.assertIs(attrs[0]["attr_val"], sint)
        self.assertEqual(u["f"]["defaults"], [])


    def test_formals_positionals_with_one_default(self):
        """
        FORMALS accepts (self threshold: 0.6) -- a typical method
        shape with one positional and one keyword default
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        sself = symbol("self")
        kthresh = keyword("threshold")
        formals = _list(sself, kthresh, 0.6)
        result = m.match((formals,))
        self.assertTrue(result.matched)
        u = _ucaps(result)
        positionals = u["f"]["positionals"]
        self.assertEqual(len(positionals), 1)
        self.assertIs(positionals[0]["name"], sself)
        defaults = u["f"]["defaults"]
        self.assertEqual(len(defaults), 1)
        self.assertIs(defaults[0]["kw"], kthresh)
        self.assertEqual(defaults[0]["default"], 0.6)


    def test_formals_pure_defaults_only(self):
        """
        FORMALS accepts (alive: "O" dead: " "), pure keyword/value
        defaults with no positional preamble
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        kalive = keyword("alive")
        kdead = keyword("dead")
        formals = _list(kalive, "O", kdead, " ")
        result = m.match((formals,))
        self.assertTrue(result.matched)
        u = _ucaps(result)
        self.assertEqual(u["f"]["positionals"], [])
        defaults = u["f"]["defaults"]
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

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        kargv = keyword("argv")
        formals = _list(kargv, None)
        result = m.match((formals,))
        self.assertTrue(result.matched)
        defaults = _ucaps(result)["f"]["defaults"]
        self.assertEqual(len(defaults), 1)
        self.assertIs(defaults[0]["kw"], kargv)
        self.assertIsNone(defaults[0]["default"])


    def test_formals_only_star(self):
        """
        FORMALS accepts (*: rest) with no positional preamble
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        rest = symbol("rest")
        formals = _list(keyword("*"), rest)
        result = m.match((formals,))
        self.assertTrue(result.matched)
        c = _ucaps(result)["f"]
        self.assertEqual(c["positionals"], [])
        self.assertIs(c["star"], rest)
        self.assertNotIn("starstar", c)


    def test_formals_only_starstar(self):
        """
        FORMALS accepts (**: kw) with no positional preamble
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        skw = symbol("kw")
        formals = _list(keyword("**"), skw)
        result = m.match((formals,))
        self.assertTrue(result.matched)
        c = _ucaps(result)["f"]
        self.assertEqual(c["positionals"], [])
        self.assertIs(c["starstar"], skw)
        self.assertNotIn("star", c)


    def test_formals_bracket_name_only(self):
        """
        FORMALS accepts a bracket positional with just a symbol such
        as ((x)); the frame holds 'name' and no other inner slots
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        sx = symbol("x")
        formals = _list(_list(sx))
        result = m.match((formals,))
        self.assertTrue(result.matched)
        positionals = _ucaps(result)["f"]["positionals"]
        self.assertEqual(len(positionals), 1)
        self.assertIs(positionals[0]["name"], sx)


    def test_formals_bracket_keyword_attributes(self):
        """
        FORMALS accepts a bracket positional with keyword attributes
        such as ((x :: T)); the frame holds 'name' and an 'attrs'
        list of keyword/value frames
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        sx = symbol("x")
        st = symbol("T")
        formals = _list(_list(sx, keyword("::"), st))
        result = m.match((formals,))
        self.assertTrue(result.matched)
        positionals = _ucaps(result)["f"]["positionals"]
        self.assertEqual(len(positionals), 1)
        self.assertIs(positionals[0]["name"], sx)
        attrs = positionals[0]["attrs"]
        self.assertIsInstance(attrs, list)
        self.assertEqual(len(attrs), 1)
        self.assertIs(attrs[0]["attr_val"], st)


    def test_formals_rejects_improper_list(self):
        """
        FORMALS rejects an improper-list formals shape such as
        (a b . c); improper formals lists are illegal in koskari
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        sb = symbol("b")
        sc = symbol("c")
        improper = cons(sa, sb, sc)
        result = m.match((improper,))
        self.assertFalse(result.matched)


    def test_formals_rejects_non_symbol_non_list(self):
        """
        FORMALS rejects a formals cell that is not a proper list, such as
        a number
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        result = m.match((42,))
        self.assertFalse(result.matched)


    def test_formals_star_segment(self):
        """
        FORMALS accepts (a *: rest) and binds 'star' to the rest symbol
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        rest = symbol("rest")
        formals = _list(sa, keyword("*"), rest)
        result = m.match((formals,))
        self.assertTrue(result.matched)
        c = _ucaps(result)["f"]
        self.assertEqual(len(c["positionals"]), 1)
        self.assertIs(c["positionals"][0]["name"], sa)
        self.assertIs(c["star"], rest)
        self.assertNotIn("starstar", c)


    def test_formals_starstar_segment(self):
        """
        FORMALS accepts (a **: kw) and binds 'starstar' to the kwargs
        symbol
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        kw = symbol("kw")
        formals = _list(sa, keyword("**"), kw)
        result = m.match((formals,))
        self.assertTrue(result.matched)
        c = _ucaps(result)["f"]
        self.assertIs(c["starstar"], kw)
        self.assertNotIn("star", c)


    def test_formals_default_keyword_value_pairs(self):
        """
        FORMALS accepts (a foo: 1 bar: 2) and produces two default
        frames each holding 'kw' (a keyword) and 'default' (a value)
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        foo = keyword("foo")
        bar = keyword("bar")
        formals = _list(sa, foo, 1, bar, 2)
        result = m.match((formals,))
        self.assertTrue(result.matched)
        defaults = _ucaps(result)["f"]["defaults"]
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

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        rest = symbol("rest")
        sx = symbol("x")
        ky = keyword("y")
        skw = symbol("kw")
        formals = _list(
            sa, keyword("*"), rest, sx, ky, 2, keyword("**"), skw)
        result = m.match((formals,))
        self.assertTrue(result.matched)
        u = _ucaps(result)["f"]
        self.assertIs(u["star"], rest)
        kwonly = u["kwonly"]
        self.assertIsInstance(kwonly, list)
        self.assertEqual(len(kwonly), 2)
        self.assertIs(kwonly[0]["name"], sx)
        self.assertIs(kwonly[1]["kw"], ky)
        self.assertEqual(kwonly[1]["default"], 2)
        self.assertIs(u["starstar"], skw)


    def test_formals_star_nil_marker(self):
        """
        FORMALS accepts (a *: ()) where (the keyword is followed by
        nil); the star slot binds to nil
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        formals = _list(sa, keyword("*"), nil)
        result = m.match((formals,))
        self.assertTrue(result.matched)
        self.assertIs(_ucaps(result)["f"]["star"], nil)


    def test_formals_bracket_default(self):
        """
        FORMALS accepts a bracket positional with default such as
        (a (b 1)) and the bracket frame holds name and default
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        sa = symbol("a")
        sb = symbol("b")
        formals = _list(sa, _list(sb, 1))
        result = m.match((formals,))
        self.assertTrue(result.matched)
        positionals = _ucaps(result)["f"]["positionals"]
        self.assertEqual(len(positionals), 2)
        self.assertIs(positionals[0]["name"], sa)
        self.assertIs(positionals[1]["name"], sb)
        self.assertEqual(positionals[1]["default"], 1)


    def test_formals_bracket_as_form(self):
        """
        FORMALS accepts a bracket positional (T as: name) and the
        frame holds 'type' and 'name'
        """

        pat = MATCH(FORMALS("f"))
        m = pat.build()
        st = symbol("T")
        sn = symbol("n")
        formals = _list(_list(st, keyword("as"), sn))
        result = m.match((formals,))
        self.assertTrue(result.matched)
        positionals = _ucaps(result)["f"]["positionals"]
        self.assertEqual(len(positionals), 1)
        self.assertIs(positionals[0]["type"], st)
        self.assertIs(positionals[0]["name"], sn)


class TestBODY(unittest.TestCase):


    def test_body_captures_rest_like_rest(self):
        """
        BODY captures the remainder of the source list into the named
        slot, identical to REST at the engine level
        """

        pat = MATCH(CAPTURE("head"), BODY("body"))
        m = pat.build()
        result = m.match((1, 2, 3))
        self.assertTrue(result.matched)
        self.assertEqual(_ucaps(result)["head"], 1)
        self.assertEqual(_ucaps(result)["body"], (2, 3))


class TestVALUE(unittest.TestCase):


    def test_value_captures_one_element_like_bind(self):
        """
        VALUE captures a single spine element, identical to BIND at the
        engine level
        """

        pat = MATCH(CAPTURE("head"), VALUE("expr"))
        m = pat.build()
        result = m.match((1, 2))
        self.assertTrue(result.matched)
        self.assertEqual(_ucaps(result)["head"], 1)
        self.assertEqual(_ucaps(result)["expr"], 2)


class TestComposition(unittest.TestCase):


    def test_define_with_body(self):
        """
        A define-shaped pattern composes DEFINE with a BODY scope
        region
        """

        pat = MATCH(DEFINE("name"), BODY("body"))
        m = pat.build()
        sa = symbol("foo")
        result = m.match((sa, 1, 2, 3))
        self.assertTrue(result.matched)
        self.assertIs(_ucaps(result)["name"], sa)
        self.assertEqual(_ucaps(result)["body"], (1, 2, 3))


    def test_define_with_formals_and_body(self):
        """
        A define-with-formals pattern composes DEFINE, FORMALS, and
        BODY into one matcher
        """

        pat = MATCH(DEFINE("name"), FORMALS("formals"), BODY("body"))
        m = pat.build()
        sa = symbol("foo")
        sx = symbol("x")
        sy = symbol("y")
        formals = _list(sx, sy)
        result = m.match(build_proper(sa, formals, "b1", "b2"))
        self.assertTrue(result.matched)
        u = _ucaps(result)
        self.assertIs(u["name"], sa)
        fr = result.captures["formals"]
        self.assertIsInstance(fr, DatumFrame)
        self.assertIs(fr.source, formals)
        self.assertEqual(u["body"], build_proper("b1", "b2"))
        positionals = u["formals"]["positionals"]
        self.assertEqual(len(positionals), 2)


    def test_let_shaped_pattern_with_bindings_and_body(self):
        """
        A let-shaped pattern composes BINDINGS with BODY: the first spine
        element is the whole binding block ``[[a 1] [b 2]]``, then body forms
        """

        pat = MATCH(BINDINGS("bs"), BODY("body"))
        m = pat.build()
        sa = symbol("a")
        sb = symbol("b")
        result = m.match((_list(_list(sa, 1), _list(sb, 2)), "b1", "b2"))
        self.assertTrue(result.matched)
        frames = _ucaps(result)["bs"]
        self.assertEqual(len(frames), 2)
        self.assertEqual(_ucaps(result)["body"], ("b1", "b2"))


class TestMakeMatcher(unittest.TestCase):


    def test_make_matcher_returns_native_matcher(self):
        """
        make_matcher returns a native Matcher and never a wrapper
        """

        pat = MATCH(DEFINE("name"))
        m = make_matcher(pat)
        self.assertIsInstance(m, Matcher)


    def test_make_matcher_returns_native_match_result(self):
        """
        make_matcher's matcher returns native MatchResult instances
        """

        pat = MATCH(DEFINE("name"))
        m = make_matcher(pat)
        sa = symbol("foo")
        result = m.match((sa,))
        self.assertIsInstance(result, MatchResult)


class TestCyclicPairSubject(unittest.TestCase):
    """
    A pair spine mutated into a cycle is not a proper list, and the
    subject adapter rejects it rather than walking it forever
    """

    def test_match_refuses_a_cyclic_spine(self):
        """
        Matcher.match on a cyclic pair raises instead of hanging
        """

        subject = cons(1, 2, nil)
        setcdr(cdr(subject), subject)

        m = make_matcher(MATCH(CAPTURE("a"), REST("rest")))
        with self.assertRaises(ValueError):
            m.match(subject)


    def test_match_refuses_a_self_looped_cell(self):
        """
        A single cell whose cdr is itself is the same rejection
        """

        subject = cons(1, nil)
        setcdr(subject, subject)

        m = make_matcher(MATCH(CAPTURE("a")))
        with self.assertRaises(ValueError):
            m.match(subject)


class BrokenSequence(collections.abc.Sequence):
    """
    A sequence whose reads raise
    """

    def __len__(self):
        return 1

    def __getitem__(self, i):
        raise RuntimeError("read failed")


class TestFormSubjectErrors(unittest.TestCase):
    """
    FORM descends into an element the sequence gate accepted. A read
    that raises while the sub-subject is built propagates; an improper
    pair spine is a plain no-match
    """

    def test_sequence_whose_reads_raise_propagates(self):
        m = make_matcher(MATCH(FORM(MATCH(CAPTURE("a")))))
        with self.assertRaises(RuntimeError):
            m.match((BrokenSequence(),))

    def test_improper_pair_is_a_no_match(self):
        m = make_matcher(MATCH(FORM(MATCH(CAPTURE("a"), CAPTURE("b")))))
        self.assertFalse(m.match((cons(1, 2),)).matched)
        self.assertTrue(m.match((cons(1, 2, nil),)).matched)

    def test_improper_root_subject_is_refused(self):
        m = make_matcher(MATCH(CAPTURE("a")))
        with self.assertRaises(TypeError):
            m.match(cons(1, 2))


class TestPairSubjectMutation(unittest.TestCase):
    """
    The pair subject owns every cell and car it loaded, so a predicate
    that mutates the list mid-match cannot free what the engine still
    reads; the match finishes over the list it loaded
    """

    def test_predicate_cuts_the_spine(self):
        subject = cons(1, 2, 3, 4, nil)

        def chop(v):
            setcdr(subject, nil)
            return True

        m = make_matcher(MATCH(PREDICATE("chop"), CAPTURE("b"), REST("rest")))
        result = m.match(subject, chop=chop)
        self.assertTrue(result.matched)
        caps = _ucaps(result)
        self.assertEqual(caps["b"], 2)
        self.assertEqual(list(caps["rest"].unpack()), [3, 4])
        self.assertEqual(list(subject.unpack()), [1])


    def test_predicate_swaps_the_next_car(self):
        subject = cons(1, "two", 3, nil)
        second = cdr(subject)

        def swap(v):
            setcar(second, "swapped")
            return True

        m = make_matcher(MATCH(PREDICATE("swap"), CAPTURE("b"), CAPTURE("c")))
        result = m.match(subject, swap=swap)
        self.assertTrue(result.matched)
        caps = _ucaps(result)
        self.assertEqual(caps["b"], "two")
        self.assertEqual(caps["c"], 3)
        self.assertEqual(list(subject.unpack()), [1, "swapped", 3])

# The end.
