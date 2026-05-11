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
Tests for the koskari.matcher_ng builder API: per-primitive freeze
shapes, slot/extern/const interning, build->match round-trips, eager
validation negatives, ergonomic shorthand semantics, and cross-cutting
integration.
"""


import unittest

import pytest

from koskari.lib import cons, keyword, nil, pair, symbol
from koskari.matcher_ng import (
    BIND,
    CAPTURE,
    CLASS,
    FIELD,
    FORM,
    GROUP,
    LIST,
    LITERAL,
    LITERAL_KW,
    LITERAL_SYM,
    MATCH,
    Matcher,
    MatchResult,
    NOT,
    ONEOF,
    OPTIONAL,
    PAIR,
    PREDICATE,
    REPEAT,
    REST,
    SYMBOL,
    TEST_ANY,
    TYPE,
    VALUE,
    make_matcher,
    thaw,
)


pytestmark = pytest.mark.phase_infra


# === Per-primitive freeze shape tests ===


class TestPrimitiveFreezeShapes(unittest.TestCase):


    def test_test_any_emits_any_node(self):
        """
        TEST_ANY emits a single ('TEST', 'ANY', -1) node
        """

        frozen = TEST_ANY().freeze()
        self.assertEqual(frozen[4], (("TEST", "ANY", -1),))
        self.assertEqual(frozen[5], 0)


    def test_literal_interns_const(self):
        """
        LITERAL writes its value into consts and emits a TEST/LITERAL
        node referencing that index
        """

        frozen = LITERAL(7).freeze()
        self.assertEqual(frozen[3], (7,))
        self.assertEqual(frozen[4], (("TEST", "LITERAL", 0),))


    def test_type_interns_extern(self):
        """
        TYPE writes its extern name into externs and emits a TEST/TYPE
        node referencing that index
        """

        frozen = TYPE("symbol").freeze()
        self.assertEqual(frozen[2], ("symbol",))
        self.assertEqual(frozen[4], (("TEST", "TYPE", 0),))


    def test_predicate_interns_extern(self):
        """
        PREDICATE writes its extern name into externs and emits a
        TEST/PREDICATE node referencing that index
        """

        frozen = PREDICATE("is_symbol").freeze()
        self.assertEqual(frozen[2], ("is_symbol",))
        self.assertEqual(frozen[4], (("TEST", "PREDICATE", 0),))


    def test_capture_no_subnode_emits_minus_one(self):
        """
        CAPTURE without a sub-pattern emits subnode -1
        """

        frozen = CAPTURE("x").freeze()
        self.assertEqual(frozen[1], ("x",))
        self.assertEqual(frozen[4], (("CAPTURE", 0, -1, 0, -1),))


    def test_capture_with_sub_emits_post_order(self):
        """
        CAPTURE wrapping TYPE emits the TYPE node first then a CAPTURE
        whose subnode index references the TYPE
        """

        frozen = CAPTURE("x", TYPE("symbol")).freeze()
        self.assertEqual(frozen[1], ("x",))
        self.assertEqual(frozen[2], ("symbol",))
        self.assertEqual(
            frozen[4],
            (("TEST", "TYPE", 0), ("CAPTURE", 0, 0, 0, -1)))
        self.assertEqual(frozen[5], 1)


    def test_form_emits_post_order(self):
        """
        FORM emits its subnode first then a FORM node referencing it
        """

        frozen = FORM(TEST_ANY()).freeze()
        self.assertEqual(
            frozen[4], (("TEST", "ANY", -1), ("FORM", 0)))
        self.assertEqual(frozen[5], 1)


    def test_match_collects_children(self):
        """
        MATCH emits each child then a MATCH node listing them in order
        """

        frozen = MATCH(TEST_ANY(), TEST_ANY()).freeze()
        self.assertEqual(
            frozen[4],
            (("TEST", "ANY", -1),
             ("TEST", "ANY", -1),
             ("MATCH", 0, 1)))
        self.assertEqual(frozen[5], 2)


    def test_named_group_emits_group_node_with_slot_index(self):
        """
        GROUP(slot, ...) reserves the slot name and emits a GROUP node
        with slot index then child indices
        """

        frozen = GROUP("g", CAPTURE("a")).freeze()
        self.assertEqual(frozen[1], ("g", "a"))
        self.assertEqual(
            frozen[4],
            (("CAPTURE", 1, -1, 0, -1),
             ("GROUP", 0, 0)))
        self.assertEqual(frozen[5], 1)


    def test_oneof_collects_branches(self):
        """
        ONEOF emits each branch then a ONEOF node listing them in order
        """

        frozen = ONEOF(TEST_ANY(), TEST_ANY()).freeze()
        self.assertEqual(
            frozen[4],
            (("TEST", "ANY", -1),
             ("TEST", "ANY", -1),
             ("ONEOF", 0, 1)))


    def test_repeat_emits_post_order(self):
        """
        REPEAT emits its subnode first then a REPEAT referencing it
        """

        frozen = REPEAT(TEST_ANY()).freeze()
        self.assertEqual(
            frozen[4], (("TEST", "ANY", -1), ("REPEAT", 0, 0)))


    def test_optional_emits_post_order(self):
        """
        OPTIONAL emits its subnode first then an OPTIONAL referencing it
        """

        frozen = OPTIONAL(TEST_ANY()).freeze()
        self.assertEqual(
            frozen[4], (("TEST", "ANY", -1), ("OPTIONAL", 0)))


    def test_rest_interns_slot(self):
        """
        REST writes its slot name into slots and emits a REST node
        """

        frozen = REST("rest").freeze()
        self.assertEqual(frozen[1], ("rest",))
        self.assertEqual(frozen[4], (("REST", 0, 0),))


    def test_not_emits_post_order(self):
        """
        NOT emits its subnode first then a NOT referencing it
        """

        frozen = NOT(TEST_ANY()).freeze()
        self.assertEqual(
            frozen[4], (("TEST", "ANY", -1), ("NOT", 0)))


    def test_class_empty_emits_extern_only(self):
        """
        CLASS with no field children emits ('CLASS', extern_idx)
        """

        frozen = CLASS("Point").freeze()
        self.assertEqual(frozen[2], ("Point",))
        self.assertEqual(frozen[4], (("CLASS", 0),))


    def test_class_with_positional_field(self):
        """
        CLASS containing a positional FIELD emits FIELD_POS then CLASS
        """

        frozen = CLASS("Point", FIELD(TEST_ANY())).freeze()
        self.assertEqual(
            frozen[4],
            (("TEST", "ANY", -1),
             ("FIELD_POS", 0),
             ("CLASS", 0, 1)))


    def test_class_with_keyword_field(self):
        """
        CLASS containing a keyword FIELD emits FIELD_KW carrying the
        attribute name as a literal third tuple element
        """

        frozen = CLASS(
            "Point", FIELD(TEST_ANY(), name="color")).freeze()
        self.assertEqual(
            frozen[4],
            (("TEST", "ANY", -1),
             ("FIELD_KW", 0, "color"),
             ("CLASS", 0, 1)))


# === Slot/extern/const interning tests ===


class TestInterning(unittest.TestCase):


    def test_distinct_slots_in_declaration_order(self):
        """
        Two CAPTURE patterns with distinct slot names are appended in
        declaration order
        """

        frozen = MATCH(CAPTURE("a"), CAPTURE("b")).freeze()
        self.assertEqual(frozen[1], ("a", "b"))


    def test_const_dedup_same_value(self):
        """
        Two LITERAL(7) references share const idx 0
        """

        frozen = ONEOF(LITERAL(7), LITERAL(7)).freeze()
        self.assertEqual(frozen[3], (7,))
        self.assertEqual(
            frozen[4][0], ("TEST", "LITERAL", 0))
        self.assertEqual(
            frozen[4][1], ("TEST", "LITERAL", 0))


    def test_const_type_aware_dedup(self):
        """
        Const dedup distinguishes equal-but-different-type values such
        as 7 and '7'
        """

        frozen = ONEOF(LITERAL(7), LITERAL("7")).freeze()
        self.assertEqual(frozen[3], (7, "7"))
        self.assertEqual(
            frozen[4][0], ("TEST", "LITERAL", 0))
        self.assertEqual(
            frozen[4][1], ("TEST", "LITERAL", 1))


    def test_const_zero_vs_false(self):
        """
        LITERAL(0) and LITERAL(False) intern to separate consts
        """

        frozen = ONEOF(LITERAL(0), LITERAL(False)).freeze()
        self.assertEqual(frozen[3], (0, False))


    def test_extern_dedup(self):
        """
        Two TYPE('symbol') references share extern idx 0
        """

        frozen = ONEOF(TYPE("symbol"), TYPE("symbol")).freeze()
        self.assertEqual(frozen[2], ("symbol",))


    def test_class_extern_in_declaration_order(self):
        """
        CLASS extern and inner TYPE extern are interned in declaration
        order
        """

        pat = CLASS("Point", FIELD(TYPE("int"), name="x"))
        frozen = pat.freeze()
        self.assertEqual(frozen[2], ("Point", "int"))


# === Build -> thaw -> match round-trips ===


class TestBuildAndMatch(unittest.TestCase):


    def test_build_returns_native_matcher(self):
        """
        build() yields a real Matcher instance whose tables match the
        builder's freeze
        """

        m = TEST_ANY().build()
        self.assertIsInstance(m, Matcher)


    def test_lambda_pattern_round_trip(self):
        """
        Build a lambda-shaped pattern and verify it matches the same
        subject as the hand-built test_thaw lambda example
        """

        pat = MATCH(
            CAPTURE("name", TYPE("symbol")),
            LITERAL("::"),
            TEST_ANY(),
            TEST_ANY(),
            REST("body"),
        )
        m = pat.build()
        self.assertEqual(m.slots, ("name", "body"))
        self.assertEqual(m.externs, ("symbol",))
        self.assertEqual(m.consts, ("::",))

        subj = (symbol("foo"), "::", "ret-type", "formals", "b1", "b2")
        result = m.match(subj, symbol=symbol)
        self.assertIsInstance(result, MatchResult)
        self.assertTrue(result.matched)
        self.assertIs(result.captures["name"].datum, subj[0])
        self.assertEqual(
            result.captures["body"].datum, ("b1", "b2"))


    def test_oneof_omits_other_branch_slots(self):
        """
        ONEOF binds its matching branch's slots and omits the other
        branch's slot keys
        """

        pat = ONEOF(
            MATCH(CAPTURE("xs", LITERAL("X"))),
            MATCH(CAPTURE("ys", LITERAL("Y"))),
        )
        m = pat.build()
        r1 = m.match(["X"])
        self.assertTrue(r1.matched)
        self.assertEqual(r1.captures["xs"].datum, "X")
        self.assertNotIn("ys", r1.captures)

        r2 = m.match(["Y"])
        self.assertTrue(r2.matched)
        self.assertNotIn("xs", r2.captures)
        self.assertEqual(r2.captures["ys"].datum, "Y")


    def test_rest_against_tuple_and_kospair(self):
        """
        REST captures the tail in both tuple and kospair subjects
        """

        pat = MATCH(CAPTURE("head"), REST("tail"))

        m = pat.build()

        r_tuple = m.match((1, 2, 3))
        self.assertTrue(r_tuple.matched)
        self.assertEqual(r_tuple.captures["head"].datum, 1)
        self.assertEqual(
            r_tuple.captures["tail"].datum, (2, 3))

        r_pair = m.match(cons(1, 2, 3, nil))
        self.assertTrue(r_pair.matched)
        self.assertEqual(r_pair.captures["head"].datum, 1)
        self.assertEqual(
            r_pair.captures["tail"].datum, cons(2, 3, nil))


    def test_class_round_trip(self):
        """
        CLASS with one positional and one keyword field matches an
        instance and binds slots
        """

        class Point(object):
            __match_args__ = ("x",)


        pat = CLASS(
            "Point",
            FIELD(CAPTURE("x")),
            FIELD(CAPTURE("y"), name="y"),
        )
        m = pat.build()

        p = Point()
        p.x = 10
        p.y = 20

        result = m.match((p,), Point=Point)
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["x"].datum, 10)
        self.assertEqual(result.captures["y"].datum, 20)

        result_bad = m.match((object(),), Point=Point)
        self.assertFalse(result_bad.matched)


    def test_not_is_zero_width_lookahead(self):
        """
        NOT is a zero-width lookahead: it succeeds when the inner
        pattern does not match at the cursor and consumes nothing,
        leaving the following pattern to consume the element
        """

        pat = MATCH(NOT(LITERAL("X")), CAPTURE("v"))
        m = pat.build()

        self.assertFalse(m.match(["X"]).matched)

        result = m.match(["Y"])
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["v"].datum, "Y")


    def test_optional_omits_slot_when_inner_unmatched(self):
        """
        OPTIONAL omits its capture slot when the inner pattern does not
        match, and binds it when it does
        """

        pat = MATCH(
            OPTIONAL(CAPTURE("opt", LITERAL("X"))),
            REST("tail"),
        )
        m = pat.build()

        r_present = m.match(["X", 1, 2])
        self.assertTrue(r_present.matched)
        self.assertEqual(r_present.captures["opt"].datum, "X")
        self.assertEqual(
            r_present.captures["tail"].datum, (1, 2))

        r_absent = m.match([1, 2])
        self.assertTrue(r_absent.matched)
        self.assertNotIn("opt", r_absent.captures)
        self.assertEqual(
            r_absent.captures["tail"].datum, (1, 2))


    def test_repeat_zero_or_more_with_deterministic_suffix(self):
        """
        REPEAT is zero-or-more at the engine level. It greedily
        consumes matching elements, then yields to a deterministic
        suffix
        """

        pat = MATCH(REPEAT(LITERAL("X")), CAPTURE("end", LITERAL("Z")))
        m = pat.build()

        r_many = m.match(["X", "X", "X", "Z"])
        self.assertTrue(r_many.matched)
        self.assertEqual(r_many.captures["end"].datum, "Z")

        r_one = m.match(["X", "Z"])
        self.assertTrue(r_one.matched)

        r_zero = m.match(["Z"])
        self.assertTrue(r_zero.matched)
        self.assertEqual(r_zero.captures["end"].datum, "Z")

        self.assertFalse(m.match(["X", "X"]).matched)


# === Eager validation negatives ===


class TestEagerValidation(unittest.TestCase):


    def test_oneof_requires_branches(self):
        """
        ONEOF() raises ValueError because it requires at least one
        branch
        """

        with self.assertRaises(ValueError):
            ONEOF()


    def test_literal_rejects_pattern_value(self):
        """
        LITERAL with a pattern as its value catches the typo of
        wrapping a pattern inside LITERAL
        """

        with self.assertRaises(TypeError):
            LITERAL(TEST_ANY())


    def test_capture_rejects_empty_slot(self):
        """
        CAPTURE with an empty slot name raises ValueError
        """

        with self.assertRaises(ValueError):
            CAPTURE("", TEST_ANY())


    def test_capture_rejects_non_str_slot(self):
        """
        CAPTURE with a non-str slot name raises TypeError
        """

        with self.assertRaises(TypeError):
            CAPTURE(123)


    def test_capture_rejects_non_pattern_sub(self):
        """
        CAPTURE with a non-pattern sub raises TypeError
        """

        with self.assertRaises(TypeError):
            CAPTURE("x", "not a pattern")


    def test_rest_rejects_non_str_slot(self):
        """
        REST with a non-str slot name raises TypeError
        """

        with self.assertRaises(TypeError):
            REST(123)


    def test_rest_rejects_empty_slot(self):
        """
        REST with an empty slot name raises ValueError
        """

        with self.assertRaises(ValueError):
            REST("")


    def test_type_rejects_empty_extern(self):
        """
        TYPE with an empty extern name raises ValueError
        """

        with self.assertRaises(ValueError):
            TYPE("")


    def test_type_rejects_non_str_extern(self):
        """
        TYPE with a non-str extern name raises TypeError
        """

        with self.assertRaises(TypeError):
            TYPE(123)


    def test_class_rejects_non_str_extern(self):
        """
        CLASS with a non-str extern name raises TypeError
        """

        with self.assertRaises(TypeError):
            CLASS(123)


    def test_class_rejects_non_field_child(self):
        """
        CLASS arguments that are not FIELD instances raise TypeError
        """

        with self.assertRaises(TypeError):
            CLASS("Point", TEST_ANY())


    def test_field_outside_class_freeze_rejected(self):
        """
        FIELD freeze raises ValueError when not nested inside a CLASS
        """

        with self.assertRaises(ValueError):
            FIELD(TEST_ANY()).freeze()


    def test_duplicate_plain_capture_allowed_on_freeze(self):
        """
        Two plain CAPTURE nodes for the same slot are allowed at freeze time
        so syntax-rules can repeat a pattern variable (match requires equal
        values at runtime)
        """

        pat = MATCH(CAPTURE("x"), CAPTURE("x"))
        pat.freeze()


    def test_duplicate_incompatible_capture_rejected_on_freeze(self):
        """
        Two CAPTURE nodes for the same slot with different capture kinds
        raise ValueError at freeze time
        """

        pat = MATCH(CAPTURE("x"), CAPTURE("x", TYPE("symbol")))
        with self.assertRaises(ValueError) as ctx:
            pat.freeze()
        self.assertIn("differing matcher type", str(ctx.exception))


    def test_duplicate_capture_rest_slot_rejected(self):
        """
        CAPTURE followed by REST sharing the same slot name raises
        ValueError at freeze time
        """

        pat = MATCH(CAPTURE("x"), REST("x"))
        with self.assertRaises(ValueError):
            pat.freeze()


    def test_capture_hygiene_role_negative(self):
        """
        CAPTURE rejects negative hygiene_role values
        """

        with self.assertRaises(ValueError):
            CAPTURE("x", hygiene_role=-1)


    def test_capture_hygiene_role_overflow(self):
        """
        CAPTURE rejects hygiene_role values above 0xFFFF
        """

        with self.assertRaises(ValueError):
            CAPTURE("x", hygiene_role=0x10000)


    def test_capture_hygiene_role_must_be_int(self):
        """
        CAPTURE rejects non-int hygiene_role
        """

        with self.assertRaises(TypeError):
            CAPTURE("x", hygiene_role="0")


    def test_field_rejects_empty_name(self):
        """
        FIELD(name='') raises ValueError
        """

        with self.assertRaises(ValueError):
            FIELD(TEST_ANY(), name="")


    def test_field_rejects_non_str_name(self):
        """
        FIELD(name=123) raises TypeError
        """

        with self.assertRaises(TypeError):
            FIELD(TEST_ANY(), name=123)


    def test_form_rejects_non_pattern_arg(self):
        """
        FORM with a non-pattern argument raises TypeError
        """

        with self.assertRaises(TypeError):
            FORM("not a pattern")


    def test_make_matcher_rejects_non_pattern(self):
        """
        make_matcher rejects non-pattern arguments
        """

        with self.assertRaises(TypeError):
            make_matcher("not a pattern")


# === Ergonomic shorthand semantics ===


class TestErgonomicShorthands(unittest.TestCase):


    def test_symbol_matches_symbol_rejects_int(self):
        """
        SYMBOL captures a symbol element and rejects a non-symbol
        """

        m = MATCH(SYMBOL("x")).build()

        r_ok = m.match([symbol("foo")], symbol=symbol)
        self.assertTrue(r_ok.matched)
        self.assertIs(
            r_ok.captures["x"].datum, symbol("foo"))

        self.assertFalse(m.match([7], symbol=symbol).matched)


    def test_pair_matches_pair_rejects_list(self):
        """
        PAIR captures a pair element and rejects non-pair values
        """

        m = MATCH(PAIR("p")).build()

        r_ok = m.match([cons(1, 2)], pair=pair)
        self.assertTrue(r_ok.matched)

        self.assertFalse(m.match([[1, 2]], pair=pair).matched)


    def test_literal_kw_matches_keyword_rejects_symbol(self):
        """
        LITERAL_KW(s) matches keyword(s) and rejects symbol(s)
        """

        m = MATCH(LITERAL_KW("foo")).build()

        self.assertTrue(m.match([keyword("foo")]).matched)
        self.assertFalse(m.match([symbol("foo")]).matched)


    def test_literal_sym_matches_symbol_rejects_keyword(self):
        """
        LITERAL_SYM(s) matches symbol(s) and rejects keyword(s)
        """

        m = MATCH(LITERAL_SYM("foo")).build()

        self.assertTrue(m.match([symbol("foo")]).matched)
        self.assertFalse(m.match([keyword("foo")]).matched)


    def test_list_descends_into_inner_sequence(self):
        """
        LIST descends into a single source element and matches its
        inner spec
        """

        pat = MATCH(LIST(CAPTURE("a"), CAPTURE("b")))
        m = pat.build()

        r = m.match([(1, 2)])
        self.assertTrue(r.matched)
        self.assertEqual(r.captures["a"].datum, 1)
        self.assertEqual(r.captures["b"].datum, 2)


    def test_list_with_key_captures_outer_list(self):
        """
        LIST with key= wraps the inner FORM in a CAPTURE that binds
        the matched pair-tree element to the named slot
        """

        pat = MATCH(LIST(CAPTURE("a"), CAPTURE("b"), key="lst"))
        m = pat.build()

        r = m.match([(1, 2)])
        self.assertTrue(r.matched)
        self.assertEqual(r.captures["a"].datum, 1)
        self.assertEqual(r.captures["b"].datum, 2)
        self.assertEqual(r.captures["lst"].datum, (1, 2))


    def test_bind_and_value_produce_identical_freeze(self):
        """
        BIND and VALUE produce identical frozen tuples for the same
        slot name
        """

        self.assertEqual(BIND("x").freeze(), VALUE("x").freeze())


# === Cross-cutting integration ===


class TestCrossCutting(unittest.TestCase):


    def test_freeze_is_deterministic(self):
        """
        Building the same pattern twice produces equal frozen tuples
        """

        def make_pattern():
            return MATCH(
                CAPTURE("name", TYPE("symbol")),
                LITERAL("::"),
                TEST_ANY(),
                TEST_ANY(),
                REST("body"),
            )

        self.assertEqual(make_pattern().freeze(), make_pattern().freeze())


    def test_make_matcher_equivalent_to_build(self):
        """
        make_matcher(pattern) yields an equivalent matcher to
        pattern.build()
        """

        pat = MATCH(CAPTURE("x"))
        m_a = pat.build()
        m_b = make_matcher(pat)
        self.assertEqual(m_a.slots, m_b.slots)
        self.assertEqual(m_a.externs, m_b.externs)
        self.assertEqual(m_a.consts, m_b.consts)


    def test_deep_nesting_does_not_overflow(self):
        """
        A pattern with around twenty nested OPTIONAL layers freezes
        and matches without issue
        """

        pat = TEST_ANY()
        for _ in range(20):
            pat = OPTIONAL(pat)
        pat = MATCH(pat, REST("rest"))
        m = pat.build()
        result = m.match([1, 2, 3])
        self.assertTrue(result.matched)


    def test_thaw_round_trip_through_builder(self):
        """
        A pattern built with the builder API can be re-thawed from its
        freeze tuple to obtain the same matcher
        """

        pat = MATCH(CAPTURE("a"), CAPTURE("b"))
        frozen = pat.freeze()
        m = thaw(frozen)
        result = m.match([1, 2])
        self.assertTrue(result.matched)
        self.assertEqual(
            {k: v.datum for k, v in result.captures.items()},
            {"a": 1, "b": 2})


# The end.
