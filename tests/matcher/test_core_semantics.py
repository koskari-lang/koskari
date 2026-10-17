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
tests.matcher.test_core_semantics

What a built matcher returns for the shapes whose meaning once
depended on how they were lowered: a NOT after an element, zero-width
and GROUP children of a one-element branch, a trailing ONEOF under a
wrapper, SAME at any depth or after a ONEOF or REPEAT, the per-pass
values a slot inside an uncaptured REPEAT binds, and the length check
that runs before any extern

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import DatumFrame, cons, demand, nil
from koskari.matcher import (
    APP, CAPTURE, FORM, GROUP, LITERAL, MATCH, NOT, ONEOF, OPTIONAL,
    PREDICATE, REPEAT, SAME, TEST_ANY, repeat_capture)


pytestmark = pytest.mark.phase_infra


def built(pattern, externs=None):
    return demand(pattern.build(externs))


class TestNotAfterTheElement(unittest.TestCase):
    """
    A NOT after an element tests the element after it, or the end,
    never the element already tested: alone, in a one-element MATCH
    under a CAPTURE, and in a ONEOF branch
    """

    def test_standalone(self):
        m = built(MATCH(TEST_ANY(), NOT(LITERAL(1))))
        self.assertEqual(m((1,)), {})
        self.assertIsNone(m((1, 1)))

        m = built(MATCH(TEST_ANY(), NOT(LITERAL(1)), TEST_ANY()))
        self.assertEqual(m((1, 2)), {})
        self.assertIsNone(m((1, 1)))


    def test_in_a_captured_one_element_match(self):
        m = built(MATCH(
            CAPTURE("x", MATCH(TEST_ANY(), NOT(LITERAL(2)))), LITERAL(2)))
        self.assertIsNone(m((1, 2)))

        m = built(MATCH(
            CAPTURE("x", MATCH(TEST_ANY(), NOT(LITERAL(1)))), TEST_ANY()))
        self.assertEqual(m((1, 2)), {"x": 1})
        self.assertIsNone(m((1, 1)))

        m = built(MATCH(CAPTURE("s", MATCH(TEST_ANY(), NOT(TEST_ANY())))))
        self.assertEqual(m((nil,)), {"s": nil})


    def test_in_a_oneof_branch(self):
        m = built(MATCH(ONEOF(MATCH(TEST_ANY(), NOT(LITERAL(2))), LITERAL(7))))
        self.assertEqual(m((2,)), {})

        m = built(MATCH(ONEOF(
            MATCH(TEST_ANY(), NOT(LITERAL(1))), MATCH(LITERAL(9)))))
        self.assertEqual(m((1,)), {})


    def test_wrapped_in_a_oneof_agrees_with_unwrapped(self):
        plain = built(MATCH(MATCH(CAPTURE("x"), NOT(LITERAL(1)))))
        wrapped = built(MATCH(ONEOF(
            MATCH(CAPTURE("x"), NOT(LITERAL(1))), LITERAL(9))))
        for subject in ((1,), (2,), (1, 1), (1, 2)):
            self.assertEqual(plain(subject), wrapped(subject), subject)
        self.assertEqual(wrapped((1,)), {"x": 1})


class TestOneElementBranches(unittest.TestCase):
    """
    The shapes the validator accepts all build and match: a ONEOF whose
    branches are one-element GROUPs, and zero-width children beside the
    element of a one-element MATCH
    """

    def test_oneof_of_named_groups(self):
        m = built(MATCH(ONEOF(GROUP("a", LITERAL(1)), GROUP("b", TEST_ANY()))))
        self.assertEqual(m((1,)), {"a": {}})
        self.assertEqual(m((5,)), {"b": {}})
        self.assertIsNone(m(()))
        result = m(cons(5, nil))
        self.assertIsInstance(result["b"], DatumFrame)


    def test_oneof_of_a_captured_anonymous_group(self):
        m = built(MATCH(ONEOF(
            LITERAL(1), CAPTURE("g", GROUP(None, CAPTURE("x"))))))
        self.assertEqual(m((1,)), {})
        self.assertEqual(m((5,)), {"g": {"x": 5}})
        self.assertIsNone(m(()))
        self.assertIsNone(m((1, 2)))


    def test_oneof_of_an_anonymous_group(self):
        m = built(MATCH(ONEOF(GROUP(None, CAPTURE("x")), LITERAL(1))))
        self.assertEqual(m((5,)), {})
        self.assertEqual(m((1,)), {})
        self.assertIsNone(m(()))
        self.assertIsNone(m((1, 2)))


    def test_zero_width_children_beside_the_element(self):
        m = built(MATCH(CAPTURE("x", MATCH(TEST_ANY(), OPTIONAL(LITERAL(9))))))
        self.assertEqual(m((1,)), {"x": 1})
        self.assertEqual(m((1, 9)), {"x": (1, 9)})
        self.assertIsNone(m((1, 2)))

        for empty in (MATCH(), GROUP(None)):
            m = built(MATCH(CAPTURE("x", MATCH(TEST_ANY(), empty))))
            self.assertEqual(m((1,)), {"x": 1})
            self.assertIsNone(m((1, 2)))

        m = built(MATCH(ONEOF(
            MATCH(TEST_ANY(), OPTIONAL(LITERAL(9))), LITERAL(7))))
        self.assertEqual(m((1,)), {})
        self.assertEqual(m((1, 9)), {})
        self.assertIsNone(m((1, 2)))


class TestJoinedMisses(unittest.TestCase):
    """
    Misses from nodes holding different entries on the stack may leave
    to one place; each such pattern builds and matches
    """

    def test_not_in_a_form_beside_a_oneof_with_a_not(self):
        m = built(MATCH(
            FORM(MATCH(NOT(LITERAL(2)))), ONEOF(TEST_ANY(), NOT(LITERAL(9)))))
        self.assertEqual(m(((),)), {})
        self.assertEqual(m(((), 1)), {})
        self.assertEqual(m(((), 9)), {})
        self.assertIsNone(m(((2,),)))
        self.assertIsNone(m(((1,),)))


    def test_group_with_a_not_beside_a_form_with_a_not(self):
        m = built(MATCH(OPTIONAL(MATCH(
            GROUP("g", TEST_ANY(), NOT(LITERAL(2))),
            FORM(MATCH(NOT(LITERAL(2))))))))
        self.assertEqual(m(()), {})
        self.assertEqual(m((1, ())), {"g": {}})
        self.assertIsNone(m((1, 2)))
        self.assertIsNone(m((1, (2,))))


class TestTrailingOneof(unittest.TestCase):
    """
    A ONEOF takes its first branch that matches and does not try
    another when the elements after it miss, whether it is the last
    child of its spine directly or under a GROUP, a CAPTURE, a FORM or
    another ONEOF
    """

    def test_wrappers_agree(self):
        def alt():
            return ONEOF(CAPTURE("x"), MATCH(CAPTURE("y"), CAPTURE("z")))

        shapes = (
            (MATCH(CAPTURE("a"), alt()), {"a": 1, "x": 2}),
            (MATCH(GROUP("g", CAPTURE("a"), alt())),
             {"g": {"a": 1, "x": 2}}),
            (MATCH(CAPTURE("a"), CAPTURE("o", alt())),
             {"a": 1, "o": 2, "x": 2}),
            (MATCH(CAPTURE("a"), GROUP(None, alt())), {"a": 1}),
            (MATCH(ONEOF(MATCH(CAPTURE("a"), alt()),
                         MATCH(CAPTURE("a"), alt()))),
             {"a": 1, "x": 2}),
        )
        for pattern, expected in shapes:
            m = built(pattern)
            self.assertEqual(m((1, 2)), expected)
            self.assertIsNone(m((1,)))
            self.assertIsNone(m((1, 2, 3)))

        m = built(MATCH(FORM(MATCH(CAPTURE("a"), alt()))))
        self.assertEqual(m(((1, 2),)), {"a": 1, "x": 2})
        self.assertIsNone(m(((1, 2, 3),)))


    def test_an_empty_first_branch_is_taken(self):
        for pattern in (MATCH(ONEOF(MATCH(), MATCH(TEST_ANY()))),
                        MATCH(GROUP("g", ONEOF(MATCH(), MATCH(TEST_ANY()))))):
            m = built(pattern)
            self.assertIsNone(m((1,)))


class TestSameDepths(unittest.TestCase):
    """
    SAME at depth n reads the slot of the scope n scopes out from its
    own
    """

    def test_depth_two_through_named_groups(self):
        m = built(MATCH(
            CAPTURE("x"), GROUP("g", GROUP("h", SAME("x", depth=2)))))
        self.assertEqual(m((1, 1)), {"x": 1, "g": {"h": {}}})
        self.assertIsNone(m((1, 2)))
        self.assertIsNotNone(m(cons(1, 1, nil)))
        self.assertIsNone(m(cons(1, 2, nil)))


    def test_depth_two_beside_depth_one(self):
        m = built(MATCH(CAPTURE("x"), GROUP(
            "g", CAPTURE("y"),
            GROUP("h", SAME("x", depth=2), SAME("y", depth=1)))))
        self.assertEqual(m((1, 2, 1, 2)), {"x": 1, "g": {"y": 2, "h": {}}})
        self.assertIsNone(m((1, 2, 2, 1)))


    def test_depth_two_from_a_repeat_pass(self):
        m = built(MATCH(CAPTURE("x"), repeat_capture(
            "rows", FORM(MATCH(GROUP("h", SAME("x", depth=2)))))))
        self.assertEqual(
            m((1, (1,), (1,))), {"x": 1, "rows": [{"h": {}}, {"h": {}}]})
        self.assertIsNone(m((1, (1,), (2,))))


class TestSameAfterAOneof(unittest.TestCase):
    """
    A ONEOF's branches bind into the scope around it, so a SAME after
    the ONEOF reads the binding of the branch that matched, and misses
    when that branch bound nothing
    """

    def test_reads_the_branch_binding(self):
        m = built(MATCH(ONEOF(CAPTURE("a"), LITERAL(1)), SAME("a")))
        self.assertEqual(m((5, 5)), {"a": 5})
        self.assertIsNone(m((5, 6)))


    def test_misses_when_the_branch_bound_nothing(self):
        m = built(MATCH(ONEOF(LITERAL(1), CAPTURE("a")), SAME("a")))
        self.assertIsNone(m((1, 1)))
        self.assertEqual(m((5, 5)), {"a": 5})
        self.assertIsNone(m((5, 6)))


class TestUncapturedRepeatSlots(unittest.TestCase):
    """
    A slot inside a REPEAT nothing captures binds the list of the
    values its passes bound, and a SAME after the REPEAT compares
    against that list
    """

    def test_slot_binds_the_per_pass_values(self):
        m = built(MATCH(CAPTURE("a"), REPEAT(CAPTURE("b"))))
        self.assertEqual(m((1, 2, 3)), {"a": 1, "b": [2, 3]})
        self.assertEqual(m((1,)), {"a": 1, "b": []})


    def test_slot_in_a_named_group(self):
        m = built(MATCH(GROUP("g", REPEAT(CAPTURE("a"))), CAPTURE("z")))
        self.assertEqual(m((1, 2, 3)), {"g": {"a": [1, 2]}, "z": 3})


    def test_same_after_the_repeat_reads_the_list(self):
        m = built(MATCH(REPEAT(CAPTURE("a"), min_count=1), SAME("a")))
        self.assertEqual(m((1, 2, [1, 2])), {"a": [1, 2]})
        self.assertIsNone(m((1, 2, 2)))


class TestLengthBeforeExterns(unittest.TestCase):
    """
    A subject too short or too long for a pattern misses before a
    PREDICATE or an APP runs on any of its elements
    """

    def test_predicate_on_a_subject_too_short(self):
        calls = []

        def p(v):
            calls.append(v)
            return True

        m = built(MATCH(CAPTURE("a", PREDICATE("p")), LITERAL(1)), {"p": p})
        self.assertIsNone(m((5,)))
        self.assertIsNone(m(cons(5, nil)))
        self.assertEqual(calls, [])
        self.assertEqual(m((5, 1)), {"a": 5})
        self.assertEqual(calls, [5])


    def test_predicate_on_a_subject_too_long(self):
        calls = []

        def p(v):
            calls.append(v)
            return True

        m = built(MATCH(CAPTURE("a", PREDICATE("p"))), {"p": p})
        self.assertIsNone(m((5, 6)))
        self.assertEqual(calls, [])


    def test_predicate_in_a_branch_too_wide(self):
        calls = []

        def p(v):
            calls.append(v)
            return True

        m = built(MATCH(ONEOF(
            MATCH(PREDICATE("p"), TEST_ANY(), TEST_ANY()), TEST_ANY())),
            {"p": p})
        self.assertEqual(m((5,)), {})
        self.assertEqual(calls, [])


    def test_app_on_a_subject_too_short(self):
        calls = []

        def first(v):
            calls.append(v)
            return v[0]

        m = built(
            MATCH(APP("first", CAPTURE("x")), LITERAL(1)), {"first": first})
        self.assertIsNone(m(((),)))
        self.assertEqual(calls, [])
        self.assertEqual(m(((4,), 1)), {"x": 4})


class TestAlternativesCheckOnlyTheMinimum(unittest.TestCase):
    """
    A ONEOF branch is taken when it matches at the cursor, whatever the
    length of what follows; the first matching branch is committed to
    """

    def test_a_branch_is_not_skipped_for_a_long_subject(self):
        m = built(MATCH(
            ONEOF(MATCH(TEST_ANY(), TEST_ANY()),
                  MATCH(TEST_ANY(), TEST_ANY(), TEST_ANY())),
            TEST_ANY()))
        self.assertIsNone(m((1, 2, 3, 4)))
        self.assertEqual(m((1, 2, 3)), {})


    def test_a_branch_before_a_repeat_branch_is_committed(self):
        m = built(MATCH(ONEOF(
            MATCH(TEST_ANY(), CAPTURE("a")),
            CAPTURE("r", REPEAT(TEST_ANY())))))
        self.assertIsNone(m((1, 2, 3)))
        self.assertEqual(m((1, 2)), {"a": 2})


    def test_a_captured_match_in_a_branch_is_committed(self):
        m = built(MATCH(
            ONEOF(CAPTURE("p", MATCH(TEST_ANY(), TEST_ANY())),
                  MATCH(TEST_ANY(), TEST_ANY(), TEST_ANY())),
            TEST_ANY()))
        self.assertIsNone(m((1, 2, 3, 4)))
        self.assertEqual(m((1, 2, 3)), {"p": (1, 2)})


class TestAlternativeForms(unittest.TestCase):
    """
    A FORM that is a ONEOF branch or an OPTIONAL sub-pattern checks the
    suffix's minimum before it descends, as any alternative does
    """

    def test_an_optional_form_yields_to_the_suffix(self):
        m = built(MATCH(
            OPTIONAL(FORM(MATCH(TEST_ANY()))), CAPTURE("b")))
        self.assertEqual(m(((1,),)), {"b": (1,)})
        self.assertEqual(m(((1,), 2)), {"b": 2})


    def test_a_form_branch_yields_to_the_suffix(self):
        m = built(MATCH(
            ONEOF(FORM(MATCH(TEST_ANY())), MATCH()), CAPTURE("b")))
        self.assertEqual(m(((1,),)), {"b": (1,)})
        self.assertEqual(m(((1,), 2)), {"b": 2})


class TestNameAcrossAnAlternativeAndARepeat(unittest.TestCase):
    """
    A name bound directly in one ONEOF alternative and inside a REPEAT
    in another: the alternatives share the name's one binding, and the
    REPEAT binds the list of its per-pass values
    """

    def test_direct_alternative_first(self):
        m = built(MATCH(ONEOF(
            MATCH(CAPTURE("x"), LITERAL(1)),
            REPEAT(CAPTURE("x"), min_count=1))))
        self.assertEqual(m((7, 1)), {"x": 7})
        self.assertEqual(m((7, 8, 9)), {"x": [7, 8, 9]})


    def test_repeat_alternative_first(self):
        m = built(MATCH(ONEOF(
            REPEAT(CAPTURE("x"), min_count=1),
            MATCH(CAPTURE("x"), LITERAL(1)))))
        self.assertEqual(m((7, 8, 9)), {"x": [7, 8, 9]})


    def test_two_repeat_alternatives(self):
        m = built(MATCH(ONEOF(
            MATCH(REPEAT(CAPTURE("x")), LITERAL(0)),
            REPEAT(CAPTURE("x"), min_count=1))))
        self.assertEqual(m((5, 6, 0)), {"x": [5, 6]})
        self.assertEqual(m((5, 6)), {"x": [5, 6]})


    def test_captured_repeat_alternative(self):
        m = built(MATCH(ONEOF(
            MATCH(CAPTURE("x"), LITERAL(1)),
            repeat_capture("xs", GROUP(None, CAPTURE("x"))))))
        self.assertEqual(m((7, 8, 9)), {"xs": [7, 8, 9]})
        self.assertEqual(m((7, 1)), {"x": 7})


# The end.
