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
Tests for matcher_ng phase 5 composite matchers: DEFINE, DECL, BINDING,
BINDINGS, FORMALS, USING; the BODY scope-region helper; the
MatcherProgram wrapper; locked hygiene meta wire format; and eager
validation negatives
"""


import unittest

import pytest

from koskari.exceptions import SpecialError
from koskari.lib import cons, keyword, nil, symbol
from koskari.matcher_ng import (
    BINDING,
    BINDINGS,
    BODY,
    BindingResult,
    BindingsResult,
    CAPTURE,
    DECL,
    DEFINE,
    DeclResult,
    FORMALS,
    FormalsResult,
    GROUP,
    LITERAL,
    Matcher,
    MatcherProgram,
    MatchResult,
    TEST_ANY,
    USING,
    UsingResult,
    make_matcher,
    thaw,
)


pytestmark = pytest.mark.phase_infra


SYMBOL_TYPE = type(symbol("x"))


def _binding(name, value):
    """
    Build a koskari pair representing one binding cell (name value)
    """

    return cons(name, value, nil)


def _binding_list(*pairs):
    """
    Build a koskari proper list of binding pairs
    """

    return cons(*pairs, nil) if pairs else nil


class TestDEFINE(unittest.TestCase):


    def test_define_captures_symbol(self):
        """
        DEFINE("x") captures a single symbol element
        """

        pat = GROUP(DEFINE("x"))
        prog = pat.compile()
        sa = symbol("foo")
        result = prog.match((sa,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        self.assertIs(result.captures["x"], sa)


    def test_define_rejects_non_symbol(self):
        """
        DEFINE rejects non-symbol elements at the engine level
        """

        pat = GROUP(DEFINE("x"))
        prog = pat.compile()
        result = prog.match((42,), symbol=SYMBOL_TYPE)
        self.assertFalse(result.matched)


    def test_define_meta_no_scopes(self):
        """
        DEFINE with no scope/scopes records an empty scopes tuple
        """

        pat = GROUP(DEFINE("x"))
        frozen = pat.freeze()
        meta = frozen[7]
        self.assertEqual(
            meta["binders"]["x"], {"kind": "DEFINE", "scopes": ()})
        self.assertEqual(meta["hygiene_version"], 1)


    def test_define_meta_with_body_scope(self):
        """
        DEFINE with scope='body' plus sibling BODY records the binding
        """

        pat = GROUP(DEFINE("name", scope="body"), BODY("body"))
        frozen = pat.freeze()
        meta = frozen[7]
        self.assertEqual(
            meta["binders"]["name"],
            {"kind": "DEFINE", "scopes": ("body",)})
        self.assertEqual(
            meta["scope_regions"]["body"], {"kind": "REST"})


class TestDECL(unittest.TestCase):


    def test_decl_bare_symbol(self):
        """
        DECL accepts a bare symbol; .name is the symbol, .type is None
        """

        pat = GROUP(DECL("d"))
        prog = pat.compile()
        sa = symbol("foo")
        result = prog.match((sa,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        d = result.captures["d"]
        self.assertIsInstance(d, DeclResult)
        self.assertIs(d.name, sa)
        self.assertIsNone(d.type)


    def test_decl_with_type_via_as(self):
        """
        DECL parses (TYPE as: NAME) form into name and type
        """

        pat = GROUP(DECL("d"))
        prog = pat.compile()
        sa = symbol("foo")
        st = symbol("int")
        form = cons(st, keyword("as"), sa, nil)
        result = prog.match((form,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        d = result.captures["d"]
        self.assertIsInstance(d, DeclResult)
        self.assertIs(d.name, sa)
        self.assertIs(d.type, st)


    def test_decl_rejects_number(self):
        """
        DECL post-process returns _REJECT for a number subject
        """

        pat = GROUP(DECL("d"))
        prog = pat.compile()
        result = prog.match((42,), symbol=SYMBOL_TYPE)
        self.assertFalse(result.matched)


    def test_decl_meta_default_scopes(self):
        """
        DECL records its binder entry with empty scopes by default
        """

        pat = GROUP(DECL("d"))
        frozen = pat.freeze()
        self.assertEqual(
            frozen[7]["binders"]["d"],
            {"kind": "DECL", "scopes": ()})


class TestBINDING(unittest.TestCase):


    def test_binding_name_expr(self):
        """
        BINDING parses (NAME EXPR) into BindingResult.name and .value
        """

        pat = GROUP(BINDING("b"))
        prog = pat.compile()
        sa = symbol("a")
        cell = _binding(sa, 7)
        result = prog.match((cell,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        b = result.captures["b"]
        self.assertIsInstance(b, BindingResult)
        self.assertIs(b.name, sa)
        self.assertEqual(b.value, 7)


    def test_binding_expr_as_name(self):
        """
        BINDING parses (EXPR as: NAME) shape into .name and .value
        """

        pat = GROUP(BINDING("b"))
        prog = pat.compile()
        sa = symbol("a")
        cell = cons(99, keyword("as"), sa, nil)
        result = prog.match((cell,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        b = result.captures["b"]
        self.assertIsInstance(b, BindingResult)
        self.assertIs(b.name, sa)
        self.assertEqual(b.value, 99)


    def test_binding_rejects_malformed(self):
        """
        BINDING rejects an outright malformed cell at the engine level
        """

        pat = GROUP(BINDING("b"))
        prog = pat.compile()
        result = prog.match((42,), symbol=SYMBOL_TYPE)
        self.assertFalse(result.matched)


    def test_binding_meta(self):
        """
        BINDING records its binder entry with empty scopes by default
        """

        pat = GROUP(BINDING("b"))
        frozen = pat.freeze()
        self.assertEqual(
            frozen[7]["binders"]["b"],
            {"kind": "BINDING", "scopes": ()})


class TestBINDINGS(unittest.TestCase):


    def test_bindings_two_pairs(self):
        """
        BINDINGS folds a list of two binding pairs into parallel
        names/values lists
        """

        pat = GROUP(BINDINGS("bs"))
        prog = pat.compile()
        sa = symbol("a")
        sb = symbol("b")
        bl = _binding_list(_binding(sa, 1), _binding(sb, 2))
        result = prog.match((bl,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        bs = result.captures["bs"]
        self.assertIsInstance(bs, BindingsResult)
        self.assertEqual(list(bs.names), [sa, sb])
        self.assertEqual(list(bs.values), [1, 2])


    def test_bindings_empty_nil(self):
        """
        BINDINGS accepts the empty list nil and yields empty parallel
        result lists
        """

        pat = GROUP(BINDINGS("bs"))
        prog = pat.compile()
        result = prog.match((nil,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        bs = result.captures["bs"]
        self.assertIsInstance(bs, BindingsResult)
        self.assertEqual(list(bs.names), [])
        self.assertEqual(list(bs.values), [])


    def test_bindings_duplicate_names_raise(self):
        """
        Duplicate binding names propagate SpecialError from
        gather_bindings during post-process
        """

        pat = GROUP(BINDINGS("bs"))
        prog = pat.compile()
        sa = symbol("a")
        bl = _binding_list(_binding(sa, 1), _binding(sa, 2))
        with self.assertRaises(SpecialError):
            prog.match((bl,), symbol=SYMBOL_TYPE)


    def test_bindings_mode_recorded(self):
        """
        BINDINGS records the mode kwarg on its binder hygiene entry
        """

        pat_p = GROUP(BINDINGS("bs", mode="parallel"))
        pat_s = GROUP(BINDINGS("bs", mode="sequential"))
        self.assertEqual(
            pat_p.freeze()[7]["binders"]["bs"]["mode"], "parallel")
        self.assertEqual(
            pat_s.freeze()[7]["binders"]["bs"]["mode"], "sequential")


class TestFORMALS(unittest.TestCase):


    def test_formals_bare_symbol(self):
        """
        FORMALS against a bare symbol yields star=symbol with empty
        positional/defaults/kwonly
        """

        pat = GROUP(FORMALS("f"))
        prog = pat.compile()
        sr = symbol("rest")
        result = prog.match((sr,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        f = result.captures["f"]
        self.assertIsInstance(f, FormalsResult)
        self.assertIs(f.star, sr)
        self.assertEqual(f.positional, [])


    def test_formals_three_positional(self):
        """
        FORMALS against (a b c) yields a three-element positional list
        """

        pat = GROUP(FORMALS("f"))
        prog = pat.compile()
        sa = symbol("a")
        sb = symbol("b")
        sc = symbol("c")
        formals = cons(sa, sb, sc, nil)
        result = prog.match((formals,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        f = result.captures["f"]
        self.assertEqual(list(f.positional), [sa, sb, sc])
        self.assertIsNone(f.star)


    def test_formals_with_star(self):
        """
        FORMALS against (a b *: rest) populates star
        """

        pat = GROUP(FORMALS("f"))
        prog = pat.compile()
        sa = symbol("a")
        sb = symbol("b")
        sr = symbol("rest")
        formals = cons(sa, sb, keyword("*"), sr, nil)
        result = prog.match((formals,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        f = result.captures["f"]
        self.assertEqual(list(f.positional), [sa, sb])
        self.assertIs(f.star, sr)


    def test_formals_with_starstar(self):
        """
        FORMALS against (a b *: rest **: kw) populates starstar
        """

        pat = GROUP(FORMALS("f"))
        prog = pat.compile()
        sa = symbol("a")
        sb = symbol("b")
        sr = symbol("rest")
        skw = symbol("kw")
        formals = cons(
            sa, sb, keyword("*"), sr, keyword("**"), skw, nil)
        result = prog.match((formals,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        f = result.captures["f"]
        self.assertIs(f.starstar, skw)


    def test_formals_invalid_form(self):
        """
        FORMALS reports matched=False for a number subject
        """

        pat = GROUP(FORMALS("f"))
        prog = pat.compile()
        result = prog.match((42,), symbol=SYMBOL_TYPE)
        self.assertFalse(result.matched)


class TestUSING(unittest.TestCase):


    def test_using_name_expr(self):
        """
        USING against (NAME EXPR) sets .name and .value, lhs is None
        """

        pat = GROUP(USING("u"))
        prog = pat.compile()
        sa = symbol("a")
        cell = cons(sa, 7, nil)
        result = prog.match((cell,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        u = result.captures["u"]
        self.assertIsInstance(u, UsingResult)
        self.assertIs(u.name, sa)
        self.assertEqual(u.value, 7)
        self.assertIsNone(u.lhs)


    def test_using_expr_as_name(self):
        """
        USING against (EXPR as: NAME) sets .name and .value
        """

        pat = GROUP(USING("u"))
        prog = pat.compile()
        sa = symbol("a")
        cell = cons(99, keyword("as"), sa, nil)
        result = prog.match((cell,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        u = result.captures["u"]
        self.assertIs(u.name, sa)
        self.assertEqual(u.value, 99)
        self.assertIsNone(u.lhs)


    def test_using_single_expr(self):
        """
        USING against (EXPR) sets .value with name and lhs as None
        """

        pat = GROUP(USING("u"))
        prog = pat.compile()
        cell = cons(42, nil)
        result = prog.match((cell,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        u = result.captures["u"]
        self.assertIsNone(u.name)
        self.assertEqual(u.value, 42)
        self.assertIsNone(u.lhs)


    def test_using_unpack_lhs(self):
        """
        USING against ((a b) EXPR) sets .lhs to the pair pattern,
        leaves .name as None
        """

        pat = GROUP(USING("u"))
        prog = pat.compile()
        sa = symbol("a")
        sb = symbol("b")
        lhs = cons(sa, sb, nil)
        cell = cons(lhs, 99, nil)
        result = prog.match((cell,), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        u = result.captures["u"]
        self.assertIsNone(u.name)
        self.assertEqual(u.value, 99)
        self.assertEqual(u.lhs, lhs)


    def test_using_rejects_non_pair(self):
        """
        USING rejects a number subject during post-process
        """

        pat = GROUP(USING("u"))
        prog = pat.compile()
        result = prog.match((42,), symbol=SYMBOL_TYPE)
        self.assertFalse(result.matched)


    def test_using_rejects_nil(self):
        """
        USING rejects an empty nil subject
        """

        pat = GROUP(USING("u"))
        prog = pat.compile()
        result = prog.match((nil,), symbol=SYMBOL_TYPE)
        self.assertFalse(result.matched)


    def test_using_meta(self):
        """
        USING records its binder hygiene entry with empty scopes
        """

        pat = GROUP(USING("u"))
        frozen = pat.freeze()
        self.assertEqual(
            frozen[7]["binders"]["u"],
            {"kind": "USING", "scopes": ()})


    def test_using_with_scope(self):
        """
        USING with scope='body' plus a sibling BODY records the link
        """

        pat = GROUP(USING("u", scope="body"), BODY("body"))
        frozen = pat.freeze()
        self.assertEqual(
            frozen[7]["binders"]["u"]["scopes"], ("body",))
        self.assertEqual(
            frozen[7]["scope_regions"]["body"], {"kind": "REST"})


class TestComposeAndHygiene(unittest.TestCase):


    def test_let_shape(self):
        """
        Let-shape pattern composes BINDINGS with BODY scope region
        """

        pat = GROUP(BINDINGS("bs", scope="body"), BODY("body"))
        prog = pat.compile()
        sa = symbol("a")
        sb = symbol("b")
        bl = _binding_list(_binding(sa, 1), _binding(sb, 2))
        subj = (bl, "body1", "body2")
        result = prog.match(subj, symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        bs = result.captures["bs"]
        self.assertIsInstance(bs, BindingsResult)
        self.assertEqual(list(bs.names), [sa, sb])
        self.assertEqual(list(bs.values), [1, 2])
        self.assertEqual(result.captures["body"], ("body1", "body2"))


    def test_multiple_binders_share_scope(self):
        """
        DEFINE plus DECL share a scopes tuple referencing a single
        sibling BODY region
        """

        pat = GROUP(
            DEFINE("name", scopes=("body",)),
            DECL("decl", scopes=("body",)),
            BODY("body"))
        frozen = pat.freeze()
        self.assertEqual(
            frozen[7]["binders"]["name"],
            {"kind": "DEFINE", "scopes": ("body",)})
        self.assertEqual(
            frozen[7]["binders"]["decl"],
            {"kind": "DECL", "scopes": ("body",)})


    def test_using_with_body(self):
        """
        Composite using-form with body scope produces UsingResult plus
        body REST captures
        """

        pat = GROUP(USING("ctx", scope="body"), BODY("body"))
        prog = pat.compile()
        sa = symbol("name")
        cell = cons(99, keyword("as"), sa, nil)
        result = prog.match((cell, "x", "y"), symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        u = result.captures["ctx"]
        self.assertIsInstance(u, UsingResult)
        self.assertIs(u.name, sa)
        self.assertEqual(u.value, 99)
        self.assertEqual(result.captures["body"], ("x", "y"))


    def test_meta_round_trip_via_thaw(self):
        """
        thaw of a frozen tuple preserves the hygiene meta dict
        """

        pat = GROUP(USING("u", scope="body"), BODY("body"))
        frozen = pat.freeze()
        m = thaw(frozen)
        self.assertIsInstance(m, Matcher)
        self.assertEqual(m.meta, frozen[7])


    def test_thaw_skips_post_processors(self):
        """
        thaw returns a raw Matcher whose match yields native captures
        without composite post-processing
        """

        pat = GROUP(BINDING("b"))
        frozen = pat.freeze()
        m = thaw(frozen)
        sa = symbol("a")
        cell = _binding(sa, 5)
        raw = m.match((cell,), symbol=SYMBOL_TYPE)
        self.assertTrue(raw.matched)
        self.assertIs(raw.captures["b"], cell)
        self.assertNotIsInstance(raw.captures["b"], BindingResult)


class TestEagerValidation(unittest.TestCase):


    def test_missing_scope_region_raises(self):
        """
        Composite with scope='body' but no BODY('body') in pattern
        raises ValueError at freeze
        """

        pat = GROUP(DEFINE("name", scope="body"))
        with self.assertRaises(ValueError):
            pat.freeze()


    def test_double_body_same_kind_idempotent_via_oneof(self):
        """
        Two BODY registrations with the same slot name across mutually
        exclusive ONEOF branches share the scope-region entry
        """

        from koskari.matcher_ng import ONEOF
        pat = ONEOF(
            GROUP(LITERAL("a"), BODY("body")),
            GROUP(LITERAL("b"), BODY("body")))
        frozen = pat.freeze()
        self.assertEqual(
            frozen[7]["scope_regions"]["body"], {"kind": "REST"})


    def test_duplicate_binder_raises(self):
        """
        Two composite binders on the same slot raise ValueError
        """

        pat = GROUP(DEFINE("x"), DEFINE("x"))
        with self.assertRaises(ValueError):
            pat.freeze()


    def test_define_slot_must_be_str(self):
        """
        DEFINE rejects a non-str slot at construction time
        """

        with self.assertRaises(TypeError):
            DEFINE(123)


    def test_binding_scope_type_check(self):
        """
        BINDING(scope=...) requires a str; BINDING(scopes=...) requires
        a tuple/list of str
        """

        with self.assertRaises(TypeError):
            BINDING("b", scope=123)
        with self.assertRaises(TypeError):
            BINDING("b", scopes=("body", 1))


    def test_using_typo_scope_raises(self):
        """
        USING(scope='bdy') with no matching BODY raises ValueError at
        freeze
        """

        pat = GROUP(USING("u", scope="bdy"), BODY("body"))
        with self.assertRaises(ValueError):
            pat.freeze()


    def test_orphan_body_accepted(self):
        """
        BODY without a binder declaring its slot is accepted (Phase 9
        ignores unused regions)
        """

        pat = GROUP(DEFINE("x"), BODY("y"))
        frozen = pat.freeze()
        self.assertEqual(
            frozen[7]["scope_regions"]["y"], {"kind": "REST"})


class TestMatcherProgram(unittest.TestCase):


    def test_program_forwards_native_attrs(self):
        """
        MatcherProgram exposes slots, externs, consts, meta from the
        wrapped native Matcher
        """

        pat = GROUP(CAPTURE("x", TEST_ANY()))
        prog = pat.compile()
        self.assertEqual(prog.slots, ("x",))
        self.assertEqual(prog.externs, ())
        self.assertEqual(prog.consts, ())
        self.assertEqual(prog.meta, {})


    def test_program_no_post_processors_returns_raw(self):
        """
        Without post-processors, MatcherProgram.match returns the
        native MatchResult unchanged
        """

        pat = GROUP(CAPTURE("x", TEST_ANY()))
        prog = pat.compile()
        self.assertEqual(prog.post_processors, ())
        result = prog.match(("foo",))
        self.assertIsInstance(result, MatchResult)


    def test_program_rejection_returns_unmatched(self):
        """
        When a composite post-processor returns _REJECT the program
        reports matched=False with empty captures and original meta
        """

        pat = GROUP(USING("u"))
        prog = pat.compile()
        result = prog.match((nil,), symbol=SYMBOL_TYPE)
        self.assertFalse(result.matched)
        self.assertEqual(result.captures, {})
        self.assertEqual(result.meta, prog.meta)


    def test_post_processors_in_declaration_order(self):
        """
        Post-processors fire in the order their composites are emitted
        """

        pat = GROUP(DEFINE("a"), DEFINE("b"))
        prog = pat.compile()
        slots = [s for s, _ in prog.post_processors]
        self.assertEqual(slots, ["a", "b"])


    def test_make_matcher_returns_program(self):
        """
        make_matcher returns a MatcherProgram instance (Phase 5
        behavior change) but still exposes native-style attrs
        """

        pat = GROUP(CAPTURE("x", TEST_ANY()))
        prog = make_matcher(pat)
        self.assertIsInstance(prog, MatcherProgram)
        self.assertEqual(prog.slots, ("x",))


# The end.
