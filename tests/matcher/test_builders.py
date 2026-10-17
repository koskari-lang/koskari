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
Tests for the koskari.matcher builder API: per-primitive freeze
shapes, slot/extern/const interning, build->match round-trips, eager
validation negatives, ergonomic shorthand semantics, and cross-cutting
integration.
"""


import unittest

import pytest

from koskari.lib import CoreType, cons, demand, keyword, nil, pair, symbol
from koskari.matcher import (
    BIND, CAPTURE, CLASS, FIELD, FORM, GROUP, LIST, LITERAL, LITERAL_KW,
    LITERAL_SYM, MATCH, NOT, ONEOF, OPTIONAL, PAIR, PREDICATE, REPEAT, REST,
    SYMBOL, TEST_ANY, TYPE, VALUE, MatchCompiler, make_matcher)


pytestmark = pytest.mark.phase_infra


# === Per-primitive freeze shape tests ===


class TestPrimitiveFreezeShapes(unittest.TestCase):


    def test_test_any_emits_any_node(self):
        """
        TEST_ANY emits a single ('TEST', 'ANY', -1) node
        """

        tables = MatchCompiler().lower(TEST_ANY())
        self.assertEqual(tables[3], (("TEST", "ANY", -1),))
        self.assertEqual(tables[4], 0)


    def test_literal_interns_const(self):
        """
        LITERAL writes its value into consts and emits a TEST/LITERAL
        node referencing that index
        """

        tables = MatchCompiler().lower(LITERAL(7))
        self.assertEqual(tables[2], (7,))
        self.assertEqual(tables[3], (("TEST", "LITERAL", 0),))


    def test_type_interns_extern(self):
        """
        TYPE writes its extern name into externs and emits a TEST/TYPE
        node referencing that index
        """

        tables = MatchCompiler().lower(TYPE("Point"))
        self.assertEqual(tables[1], ("Point",))
        self.assertEqual(tables[3], (("TEST", "TYPE", 0),))


    def test_core_type_is_an_ordinal_not_an_extern(self):
        """
        A core type name lowers to a TEST/CORE node carrying the
        CoreType ordinal and interns nothing
        """

        for name in ("pair", "symbol", "keyword", "int", "str", "tuple", "list"):
            tables = MatchCompiler().lower(TYPE(name))
            self.assertEqual(tables[1], (), name)
            self.assertEqual(
                tables[3], (("TEST", "CORE", int(CoreType[name.upper()])),),
                name)


    def test_predicate_interns_extern(self):
        """
        PREDICATE writes its extern name into externs and emits a
        TEST/PREDICATE node referencing that index
        """

        tables = MatchCompiler().lower(PREDICATE("is_symbol"))
        self.assertEqual(tables[1], ("is_symbol",))
        self.assertEqual(tables[3], (("TEST", "PREDICATE", 0),))


    def test_capture_no_subnode_emits_minus_one(self):
        """
        CAPTURE without a sub-pattern emits subnode -1
        """

        tables = MatchCompiler().lower(CAPTURE("x"))
        self.assertEqual(tables[0], ("x",))
        self.assertEqual(tables[3], (("CAPTURE", 0, -1),))


    def test_capture_with_sub_emits_post_order(self):
        """
        CAPTURE wrapping TYPE emits the TYPE node first then a CAPTURE
        whose subnode index references the TYPE
        """

        tables = MatchCompiler().lower(CAPTURE("x", TYPE("Point")))
        self.assertEqual(tables[0], ("x",))
        self.assertEqual(tables[1], ("Point",))
        self.assertEqual(
            tables[3],
            (("TEST", "TYPE", 0), ("CAPTURE", 0, 0)))
        self.assertEqual(tables[4], 1)


    def test_form_emits_post_order(self):
        """
        FORM emits its subnode first then a FORM node referencing it
        """

        tables = MatchCompiler().lower(FORM(TEST_ANY()))
        self.assertEqual(
            tables[3], (("TEST", "ANY", -1), ("FORM", 0)))
        self.assertEqual(tables[4], 1)


    def test_match_collects_children(self):
        """
        MATCH emits each child then a MATCH node listing them in order
        """

        tables = MatchCompiler().lower(MATCH(TEST_ANY(), TEST_ANY()))
        self.assertEqual(
            tables[3],
            (("TEST", "ANY", -1),
             ("TEST", "ANY", -1),
             ("MATCH", 0, 1)))
        self.assertEqual(tables[4], 2)


    def test_named_group_emits_group_node_with_slot_index(self):
        """
        GROUP(slot, ...) reserves the slot name and lowers to GROUP
        children plus a wrapping CAPTURE for the outer slot
        """

        tables = MatchCompiler().lower(GROUP("g", CAPTURE("a")))
        self.assertEqual(tables[0], ("g", "a"))
        self.assertEqual(
            tables[3],
            (("CAPTURE", 1, -1),
             ("GROUP", 0),
             ("CAPTURE", 0, 1)))
        self.assertEqual(tables[4], 2)


    def test_rest_emits_rest_then_capture_when_named(self):
        """
        REST with a slot lowers to REST(min_tail) then CAPTURE
        """

        tables = MatchCompiler().lower(REST("rest"))
        self.assertEqual(tables[0], ("rest",))
        self.assertEqual(
            tables[3], (("REST", 0), ("CAPTURE", 0, 0)))


    def test_oneof_collects_branches(self):
        """
        ONEOF emits each branch then a ONEOF node listing them in order
        """

        tables = MatchCompiler().lower(ONEOF(TEST_ANY(), TEST_ANY()))
        self.assertEqual(
            tables[3],
            (("TEST", "ANY", -1),
             ("TEST", "ANY", -1),
             ("ONEOF", 0, 1)))


    def test_repeat_emits_post_order(self):
        """
        REPEAT emits its subnode first then a REPEAT referencing it
        """

        tables = MatchCompiler().lower(REPEAT(TEST_ANY()))
        self.assertEqual(
            tables[3], (("TEST", "ANY", -1), ("REPEAT", 0, 0)))


    def test_optional_emits_post_order(self):
        """
        OPTIONAL emits its subnode first then an OPTIONAL referencing it
        """

        tables = MatchCompiler().lower(OPTIONAL(TEST_ANY()))
        self.assertEqual(
            tables[3], (("TEST", "ANY", -1), ("OPTIONAL", 0)))


    def test_not_emits_post_order(self):
        """
        NOT emits its subnode first then a NOT referencing it
        """

        tables = MatchCompiler().lower(NOT(TEST_ANY()))
        self.assertEqual(
            tables[3], (("TEST", "ANY", -1), ("NOT", 0)))


    def test_class_empty_emits_extern_only(self):
        """
        CLASS with no field children emits ('CLASS', extern_idx)
        """

        tables = MatchCompiler().lower(CLASS("Point"))
        self.assertEqual(tables[1], ("Point",))
        self.assertEqual(tables[3], (("CLASS", 0),))


    def test_class_with_positional_field(self):
        """
        CLASS containing a positional FIELD emits FIELD_POS then CLASS
        """

        tables = MatchCompiler().lower(CLASS("Point", FIELD(TEST_ANY())))
        self.assertEqual(
            tables[3],
            (("TEST", "ANY", -1),
             ("FIELD_POS", 0),
             ("CLASS", 0, 1)))


    def test_class_with_keyword_field(self):
        """
        CLASS containing a keyword FIELD emits FIELD_KW carrying the
        attribute name as a literal third tuple element
        """

        tables = MatchCompiler().lower(CLASS(
            "Point", FIELD(TEST_ANY(), name="color")))
        self.assertEqual(
            tables[3],
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

        tables = MatchCompiler().lower(MATCH(CAPTURE("a"), CAPTURE("b")))
        self.assertEqual(tables[0], ("a", "b"))


    def test_const_dedup_same_value(self):
        """
        Two LITERAL(7) references share const idx 0
        """

        tables = MatchCompiler().lower(ONEOF(LITERAL(7), LITERAL(7)))
        self.assertEqual(tables[2], (7,))
        self.assertEqual(
            tables[3][0], ("TEST", "LITERAL", 0))
        self.assertEqual(
            tables[3][1], ("TEST", "LITERAL", 0))


    def test_const_type_aware_dedup(self):
        """
        Const dedup distinguishes equal-but-different-type values such
        as 7 and '7'
        """

        tables = MatchCompiler().lower(ONEOF(LITERAL(7), LITERAL("7")))
        self.assertEqual(tables[2], (7, "7"))
        self.assertEqual(
            tables[3][0], ("TEST", "LITERAL", 0))
        self.assertEqual(
            tables[3][1], ("TEST", "LITERAL", 1))


    def test_const_zero_vs_false(self):
        """
        LITERAL(0) and LITERAL(False) intern to separate consts
        """

        tables = MatchCompiler().lower(ONEOF(LITERAL(0), LITERAL(False)))
        self.assertEqual(tables[2], (0, False))


    def test_extern_dedup(self):
        """
        Two TYPE('Point') references share extern idx 0
        """

        tables = MatchCompiler().lower(ONEOF(TYPE("Point"), TYPE("Point")))
        self.assertEqual(tables[1], ("Point",))


    def test_class_extern_in_declaration_order(self):
        """
        CLASS extern and inner TYPE extern are interned in declaration
        order
        """

        pat = CLASS("Point", FIELD(TYPE("Coord"), name="x"))
        tables = MatchCompiler().lower(pat)
        self.assertEqual(tables[1], ("Point", "Coord"))


# === Build -> match round-trips ===


class TestBuildAndMatch(unittest.TestCase):


    def test_lambda_pattern_round_trip(self):
        """
        Build a lambda-shaped pattern and verify it matches a
        lambda-shaped subject
        """

        pat = MATCH(
            CAPTURE("name", TYPE("symbol")),
            LITERAL("::"),
            TEST_ANY(),
            TEST_ANY(),
            REST("body"),
        )
        m = pat.build({"symbol": symbol})

        subj = (symbol("foo"), "::", "ret-type", "formals", "b1", "b2")
        result = demand(m)(subj)
        self.assertIsNotNone(result)
        self.assertIs(result["name"], subj[0])
        self.assertEqual(
            result["body"], ("b1", "b2"))


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
        r1 = demand(m)(["X"])
        self.assertIsNotNone(r1)
        self.assertEqual(r1["xs"], "X")
        self.assertNotIn("ys", r1)

        r2 = demand(m)(["Y"])
        self.assertIsNotNone(r2)
        self.assertNotIn("xs", r2)
        self.assertEqual(r2["ys"], "Y")


    def test_rest_against_tuple_and_kospair(self):
        """
        REST captures the tail in both tuple and kospair subjects
        """

        pat = MATCH(CAPTURE("head"), REST("tail"))

        m = pat.build()

        r_tuple = demand(m)((1, 2, 3))
        self.assertIsNotNone(r_tuple)
        self.assertEqual(r_tuple["head"], 1)
        self.assertEqual(
            r_tuple["tail"], (2, 3))

        r_pair = demand(m)(cons(1, 2, 3, nil))
        self.assertIsNotNone(r_pair)
        self.assertEqual(r_pair["head"].datum, 1)
        self.assertEqual(
            r_pair["tail"].datum, cons(2, 3, nil))


    def test_empty_rest_before_suffix_binds_empty(self):
        """
        A REST that consumes nothing ahead of a fixed suffix binds the
        empty remainder, not the suffix; it used to bind the whole tail
        """

        pat = MATCH(CAPTURE("a"), REST("mid"), CAPTURE("b"))
        m = pat.build()

        r_tuple = demand(m)((1, 2))
        self.assertIsNotNone(r_tuple)
        self.assertEqual(r_tuple["a"], 1)
        self.assertEqual(r_tuple["mid"], ())
        self.assertEqual(r_tuple["b"], 2)

        r_pair = demand(m)(cons(1, 2, nil))
        self.assertIsNotNone(r_pair)
        self.assertIs(r_pair["mid"].datum, nil)
        self.assertEqual(r_pair["b"].datum, 2)

        r_three = demand(m)((1, 2, 3))
        self.assertIsNotNone(r_three)
        self.assertEqual(len(r_three["mid"]), 1)
        self.assertEqual(r_three["b"], 3)


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
        m = pat.build({"Point": Point})

        p = Point()
        p.x = 10
        p.y = 20

        result = demand(m)((p,))
        self.assertIsNotNone(result)
        self.assertEqual(result["x"], 10)
        self.assertEqual(result["y"], 20)

        result_bad = demand(m)((object(),))
        self.assertIsNone(result_bad)


    def test_not_is_zero_width_lookahead(self):
        """
        NOT is a zero-width lookahead: it succeeds when the inner
        pattern does not match at the cursor and consumes nothing,
        leaving the following pattern to consume the element
        """

        pat = MATCH(NOT(LITERAL("X")), CAPTURE("v"))
        m = pat.build()

        self.assertIsNone(demand(m)(["X"]))

        result = demand(m)(["Y"])
        self.assertIsNotNone(result)
        self.assertEqual(result["v"], "Y")


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

        r_present = demand(m)(["X", 1, 2])
        self.assertIsNotNone(r_present)
        self.assertEqual(r_present["opt"], "X")
        self.assertEqual(
            r_present["tail"], (1, 2))

        r_absent = demand(m)([1, 2])
        self.assertIsNotNone(r_absent)
        self.assertNotIn("opt", r_absent)
        self.assertEqual(
            r_absent["tail"], (1, 2))


    def test_repeat_zero_or_more_with_deterministic_suffix(self):
        """
        REPEAT is zero-or-more at the engine level. It greedily
        consumes matching elements, then yields to a deterministic
        suffix
        """

        pat = MATCH(REPEAT(LITERAL("X")), CAPTURE("end", LITERAL("Z")))
        m = pat.build()

        r_many = demand(m)(["X", "X", "X", "Z"])
        self.assertIsNotNone(r_many)
        self.assertEqual(r_many["end"], "Z")

        r_one = demand(m)(["X", "Z"])
        self.assertIsNotNone(r_one)

        r_zero = demand(m)(["Z"])
        self.assertIsNotNone(r_zero)
        self.assertEqual(r_zero["end"], "Z")

        self.assertIsNone(demand(m)(["X", "X"]))


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
            MatchCompiler().lower(FIELD(TEST_ANY()))


    def test_duplicate_plain_capture_rejected_on_freeze(self):
        """
        Two plain CAPTURE nodes for the same slot raise at freeze; use SAME
        """

        pat = MATCH(CAPTURE("x"), CAPTURE("x"))
        with self.assertRaises(ValueError) as ctx:
            MatchCompiler().lower(pat)
        self.assertIn("SAME", str(ctx.exception))


    def test_same_references_prior_slot_freezes(self):
        """
        MATCH(BIND x, SAME x) lowers and freezes
        """

        from koskari.matcher import SAME

        pat = MATCH(CAPTURE("x"), SAME("x"))
        MatchCompiler().lower(pat)


    def test_duplicate_incompatible_capture_rejected_on_freeze(self):
        """
        Two CAPTURE nodes for the same slot with different capture kinds
        raise ValueError at freeze time
        """

        pat = MATCH(CAPTURE("x"), CAPTURE("x", TYPE("symbol")))
        with self.assertRaises(ValueError) as ctx:
            MatchCompiler().lower(pat)
        self.assertIn("differing matcher type", str(ctx.exception))


    def test_duplicate_capture_rest_slot_rejected(self):
        """
        CAPTURE followed by REST sharing the same slot name raises
        ValueError at freeze time
        """

        pat = MATCH(CAPTURE("x"), REST("x"))
        with self.assertRaises(ValueError):
            MatchCompiler().lower(pat)


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

        m = MATCH(SYMBOL("x")).build({"symbol": symbol})

        r_ok = demand(m)([symbol("foo")])
        self.assertIsNotNone(r_ok)
        self.assertIs(
            r_ok["x"], symbol("foo"))

        self.assertIsNone(demand(m)([7]))


    def test_pair_matches_pair_rejects_list(self):
        """
        PAIR captures a pair element and rejects non-pair values
        """

        m = MATCH(PAIR("p")).build({"pair": pair})

        r_ok = demand(m)([cons(1, 2)])
        self.assertIsNotNone(r_ok)

        self.assertIsNone(demand(m)([[1, 2]]))


    def test_literal_kw_matches_keyword_rejects_symbol(self):
        """
        LITERAL_KW(s) matches keyword(s) and rejects symbol(s)
        """

        m = MATCH(LITERAL_KW("foo")).build()

        self.assertIsNotNone(demand(m)([keyword("foo")]))
        self.assertIsNone(demand(m)([symbol("foo")]))


    def test_literal_sym_matches_symbol_rejects_keyword(self):
        """
        LITERAL_SYM(s) matches symbol(s) and rejects keyword(s)
        """

        m = MATCH(LITERAL_SYM("foo")).build()

        self.assertIsNotNone(demand(m)([symbol("foo")]))
        self.assertIsNone(demand(m)([keyword("foo")]))


    def test_list_descends_into_inner_sequence(self):
        """
        LIST descends into a single source element and matches its
        inner spec
        """

        pat = MATCH(LIST(CAPTURE("a"), CAPTURE("b")))
        m = pat.build()

        r = demand(m)([(1, 2)])
        self.assertIsNotNone(r)
        self.assertEqual(r["a"], 1)
        self.assertEqual(r["b"], 2)


    def test_list_with_key_captures_outer_list(self):
        """
        LIST with key= wraps the inner FORM in a CAPTURE that binds
        the matched pair-tree element to the named slot
        """

        pat = MATCH(LIST(CAPTURE("a"), CAPTURE("b"), key="lst"))
        m = pat.build()

        r = demand(m)([(1, 2)])
        self.assertIsNotNone(r)
        self.assertEqual(r["a"], 1)
        self.assertEqual(r["b"], 2)
        self.assertEqual(r["lst"], (1, 2))


    def test_bind_and_value_produce_identical_freeze(self):
        """
        BIND and VALUE produce identical tables for the same
        slot name
        """

        self.assertEqual(
            MatchCompiler().lower(BIND("x")),
            MatchCompiler().lower(VALUE("x")))


# === Cross-cutting integration ===


class TestCrossCutting(unittest.TestCase):


    def test_freeze_is_deterministic(self):
        """
        Lowering the same pattern twice produces equal tables
        """

        def make_pattern():
            return MATCH(
                CAPTURE("name", TYPE("symbol")),
                LITERAL("::"),
                TEST_ANY(),
                TEST_ANY(),
                REST("body"),
            )

        self.assertEqual(
            MatchCompiler().lower(make_pattern()),
            MatchCompiler().lower(make_pattern()))


    def test_make_matcher_equivalent_to_build(self):
        """
        make_matcher(pattern) yields an equivalent matcher to
        pattern.build()
        """

        pat = MATCH(CAPTURE("x"))
        m_a = pat.build()
        m_b = make_matcher(pat)
        self.assertEqual(demand(m_a)((1,)), demand(m_b)((1,)))
        self.assertEqual(demand(m_a)((1,)), {"x": 1})


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
        result = demand(m)([1, 2, 3])
        self.assertIsNotNone(result)


# The end.
