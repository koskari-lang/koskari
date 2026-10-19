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
Tests for the koskari.matcher builder API: per-primitive matches and
misses, slot/extern/const interning, build->match round-trips, eager
validation negatives, ergonomic shorthand semantics, and cross-cutting
integration.
"""


import unittest

import pytest

from koskari.lib import cons, demand, keyword, nil, pair, symbol
from koskari.matcher import (
    BIND, CAPTURE, CLASS, FIELD, FORM, GROUP, LIST, LITERAL, LITERAL_KW,
    LITERAL_SYM, MATCH, NOT, ONEOF, OPTIONAL, PAIR, PREDICATE, REPEAT, REST,
    SYMBOL, TEST_ANY, TYPE, VALUE, make_matcher, pattern_slot_names)
from koskari.matcher.tables import TableBuilder
from koskari.types import is_symbol


pytestmark = pytest.mark.phase_infra


# === Per-primitive matches and misses ===


class Point:
    __match_args__ = ("x", "y")

    def __init__(self, x=0, y=0, color=None):
        self.x = x
        self.y = y
        if color is not None:
            self.color = color


class TestPrimitiveMatches(unittest.TestCase):


    def test_test_any_matches_any_one_element(self):
        """
        TEST_ANY matches one element of any value
        """

        m = demand(make_matcher(MATCH(TEST_ANY())))
        self.assertEqual(m((1,)), {})
        self.assertEqual(m(("x",)), {})
        self.assertIsNone(m(()))
        self.assertIsNone(m((1, 2)))


    def test_literal_matches_its_value(self):
        """
        LITERAL matches an element equal to its value, of its type
        """

        m = demand(make_matcher(MATCH(LITERAL(7))))
        self.assertEqual(m((7,)), {})
        self.assertIsNone(m((8,)))
        self.assertIsNone(m(("7",)))


    def test_type_tests_the_extern(self):
        """
        TYPE matches an instance of the type its extern names, and the
        extern must be given at build
        """

        m = demand(make_matcher(MATCH(TYPE("Point")), {"Point": Point}))
        self.assertEqual(m((Point(),)), {})
        self.assertIsNone(m((1,)))
        with self.assertRaisesRegex(TypeError, "missing extern"):
            MATCH(TYPE("Point")).build()


    def test_core_type_is_an_ordinal_not_an_extern(self):
        """
        A core type name needs no extern: it builds with none and
        matches a value of that core type
        """

        for name, value in (
                ("pair", cons(1, nil)),
                ("symbol", symbol("a")),
                ("keyword", keyword("k")),
                ("int", 1),
                ("str", "s"),
                ("tuple", (1,)),
                ("list", [1])):
            m = demand(make_matcher(MATCH(TYPE(name))))
            self.assertEqual(m((value,)), {}, name)
            self.assertIsNone(m((1.5,)), name)


    def test_predicate_calls_the_extern(self):
        """
        PREDICATE matches an element its extern returns true for
        """

        m = demand(make_matcher(
            MATCH(PREDICATE("is_symbol")), {"is_symbol": is_symbol}))
        self.assertEqual(m((symbol("a"),)), {})
        self.assertIsNone(m((1,)))


    def test_capture_no_subnode_binds_one_element(self):
        """
        CAPTURE without a sub-pattern binds any one element
        """

        m = demand(make_matcher(MATCH(CAPTURE("x"))))
        self.assertEqual(m((1,)), {"x": 1})
        self.assertIsNone(m(()))


    def test_capture_with_sub_binds_what_the_sub_matched(self):
        """
        CAPTURE wrapping TYPE binds the element the TYPE matched
        """

        m = demand(make_matcher(
            MATCH(CAPTURE("x", TYPE("Point"))), {"Point": Point}))
        p = Point()
        self.assertIs(m((p,))["x"], p)
        self.assertIsNone(m((1,)))


    def test_form_matches_a_nested_sequence(self):
        """
        FORM matches an element that is a sequence its sub-pattern
        matches all of; a str is not a sequence
        """

        m = demand(make_matcher(MATCH(FORM(TEST_ANY()))))
        self.assertEqual(m(((1,),)), {})
        self.assertEqual(m(([1],)), {})
        self.assertIsNone(m(((1, 2),)))
        self.assertIsNone(m((1,)))
        self.assertIsNone(m(("a",)))


    def test_match_runs_its_children_in_order(self):
        """
        MATCH matches its children one after another and the whole of
        the subject
        """

        m = demand(make_matcher(MATCH(LITERAL(1), LITERAL(2))))
        self.assertEqual(m((1, 2)), {})
        self.assertIsNone(m((2, 1)))
        self.assertIsNone(m((1,)))
        self.assertIsNone(m((1, 2, 3)))


    def test_named_group_captures_its_children(self):
        """
        GROUP(slot, ...) binds the slot to its children's captures
        """

        m = demand(make_matcher(MATCH(GROUP("g", CAPTURE("a")))))
        self.assertEqual(m((1,)), {"g": {"a": 1}})
        self.assertIsNone(m(()))


    def test_rest_binds_the_remaining_elements(self):
        """
        REST with a slot binds the remaining elements, none included
        """

        m = demand(make_matcher(MATCH(REST("rest"))))
        self.assertEqual(m((1, 2)), {"rest": (1, 2)})
        self.assertEqual(m(()), {"rest": ()})


    def test_oneof_takes_the_first_branch_that_matches(self):
        """
        ONEOF tries its branches in order and takes the first that
        matches
        """

        m = demand(make_matcher(MATCH(ONEOF(LITERAL(1), CAPTURE("x")))))
        self.assertEqual(m((1,)), {})
        self.assertEqual(m((2,)), {"x": 2})
        self.assertIsNone(m(()))


    def test_repeat_matches_zero_or_more(self):
        """
        REPEAT matches its sub-pattern any number of times
        """

        m = demand(make_matcher(MATCH(REPEAT(LITERAL(1)))))
        self.assertEqual(m(()), {})
        self.assertEqual(m((1, 1, 1)), {})
        self.assertIsNone(m((1, 2)))


    def test_optional_matches_zero_or_one(self):
        """
        OPTIONAL matches its sub-pattern once or not at all
        """

        m = demand(make_matcher(MATCH(OPTIONAL(LITERAL(1)), LITERAL(2))))
        self.assertEqual(m((2,)), {})
        self.assertEqual(m((1, 2)), {})
        self.assertIsNone(m((1, 1, 2)))


    def test_not_consumes_nothing(self):
        """
        NOT succeeds where its sub-pattern does not match and consumes
        no element
        """

        m = demand(make_matcher(MATCH(NOT(LITERAL(1)), TEST_ANY())))
        self.assertEqual(m((2,)), {})
        self.assertIsNone(m((1,)))
        self.assertIsNone(m(()))


    def test_class_empty_is_an_isinstance_test(self):
        """
        CLASS with no field children matches an instance of its type
        """

        m = demand(make_matcher(MATCH(CLASS("Point")), {"Point": Point}))
        self.assertEqual(m((Point(),)), {})
        self.assertIsNone(m((1,)))


    def test_class_with_positional_field(self):
        """
        A positional FIELD matches the attribute at its __match_args__
        position
        """

        m = demand(make_matcher(
            MATCH(CLASS("Point", FIELD(LITERAL(3)))), {"Point": Point}))
        self.assertEqual(m((Point(3),)), {})
        self.assertIsNone(m((Point(4),)))


    def test_class_with_keyword_field(self):
        """
        A keyword FIELD matches the attribute it names, and misses an
        instance without it
        """

        m = demand(make_matcher(
            MATCH(CLASS("Point", FIELD(LITERAL("red"), name="color"))),
            {"Point": Point}))
        self.assertEqual(m((Point(color="red"),)), {})
        self.assertIsNone(m((Point(color="blue"),)))
        self.assertIsNone(m((Point(),)))


# === Slot/extern/const interning tests ===


class TestInterning(unittest.TestCase):


    def test_distinct_slots_in_declaration_order(self):
        """
        Two CAPTURE patterns with distinct slot names are formals in
        declaration order, and the captures keep that order
        """

        pattern = MATCH(CAPTURE("a"), CAPTURE("b"))
        self.assertEqual(pattern_slot_names(pattern), ["a", "b"])
        captures = demand(make_matcher(pattern))((1, 2))
        self.assertEqual(list(captures), ["a", "b"])


    def test_const_dedup_same_value(self):
        """
        Two LITERAL(7) references share const idx 0
        """

        tables = TableBuilder().lower(ONEOF(LITERAL(7), LITERAL(7)))
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

        tables = TableBuilder().lower(ONEOF(LITERAL(7), LITERAL("7")))
        self.assertEqual(tables[2], (7, "7"))
        self.assertEqual(
            tables[3][0], ("TEST", "LITERAL", 0))
        self.assertEqual(
            tables[3][1], ("TEST", "LITERAL", 1))


    def test_const_zero_vs_false(self):
        """
        LITERAL(0) and LITERAL(False) intern to separate consts
        """

        tables = TableBuilder().lower(ONEOF(LITERAL(0), LITERAL(False)))
        self.assertEqual(tables[2], (0, False))


    def test_extern_dedup(self):
        """
        Two TYPE('Point') references share extern idx 0
        """

        tables = TableBuilder().lower(ONEOF(TYPE("Point"), TYPE("Point")))
        self.assertEqual(tables[1], ("Point",))


    def test_class_extern_in_declaration_order(self):
        """
        CLASS extern and inner TYPE extern are interned in declaration
        order
        """

        pat = CLASS("Point", FIELD(TYPE("Coord"), name="x"))
        tables = TableBuilder().lower(pat)
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


    def test_field_outside_class_refused_at_build(self):
        """
        build raises ValueError for a FIELD not nested inside a CLASS
        """

        with self.assertRaises(ValueError):
            FIELD(TEST_ANY()).build()


    def test_duplicate_plain_capture_refused_at_build(self):
        """
        Two plain CAPTURE nodes for the same slot raise at build; use SAME
        """

        pat = MATCH(CAPTURE("x"), CAPTURE("x"))
        with self.assertRaises(ValueError) as ctx:
            pat.build()
        self.assertIn("SAME", str(ctx.exception))


    def test_same_references_prior_slot(self):
        """
        MATCH(CAPTURE x, SAME x) builds and matches only an element
        equal to the earlier capture
        """

        from koskari.matcher import SAME

        m = demand(MATCH(CAPTURE("x"), SAME("x")).build())
        self.assertEqual(m((1, 1)), {"x": 1})
        self.assertIsNone(m((1, 2)))


    def test_duplicate_incompatible_capture_refused_at_build(self):
        """
        Two CAPTURE nodes for the same slot with different capture kinds
        raise ValueError at build
        """

        pat = MATCH(CAPTURE("x"), CAPTURE("x", TYPE("symbol")))
        with self.assertRaises(ValueError) as ctx:
            pat.build()
        self.assertIn("differing matcher type", str(ctx.exception))


    def test_duplicate_capture_rest_slot_rejected(self):
        """
        CAPTURE followed by REST sharing the same slot name raises
        ValueError at build
        """

        pat = MATCH(CAPTURE("x"), REST("x"))
        with self.assertRaises(ValueError):
            pat.build()


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


    def test_bind_and_value_capture_the_same_element(self):
        """
        BIND and VALUE capture the same one element for the same slot
        name
        """

        bind = demand(make_matcher(MATCH(BIND("x"))))
        value = demand(make_matcher(MATCH(VALUE("x"))))
        for subject in ((1,), (), (1, 2)):
            self.assertEqual(bind(subject), value(subject), subject)
        self.assertEqual(bind((1,)), {"x": 1})
        spine = cons(symbol("a"), nil)
        self.assertIs(bind(spine)["x"].datum, value(spine)["x"].datum)


# === Cross-cutting integration ===


class TestCrossCutting(unittest.TestCase):


    def test_build_is_deterministic(self):
        """
        Building the same pattern twice gives matchers that return
        equal captures and miss alike
        """

        def make_pattern():
            return MATCH(
                CAPTURE("name", TYPE("symbol")),
                LITERAL("::"),
                TEST_ANY(),
                TEST_ANY(),
                REST("body"),
            )

        first = demand(make_matcher(make_pattern()))
        second = demand(make_matcher(make_pattern()))
        hit = (symbol("f"), "::", 1, 2, 3, 4)
        self.assertEqual(first(hit), second(hit))
        self.assertEqual(
            first(hit), {"name": symbol("f"), "body": (3, 4)})
        self.assertIsNone(first((symbol("f"), ":", 1, 2)))
        self.assertIsNone(second((symbol("f"), ":", 1, 2)))


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
        A pattern with around twenty nested OPTIONAL layers builds
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
