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
The standalone matcher function beside the Matcher over it: the same
frozen matcher called bare and through Matcher.match, over the same
subjects, must yield the same captures in the same shapes, Datum and
DatumFrame included, and the shapes are the ones the compiler reads.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import Datum, DatumFrame, ModuleNamespace, cons, keyword, nil, symbol
from koskari.match_code import compile_match_function
from koskari.matcher import (
    BIND, BINDINGS, BODY, CALLEDBY, CAPTURE, CLASS, FIELD, FORM, FORMALS,
    LITERAL, LITERAL_KW, MATCH, NOT, ONEOF, OPTIONAL, PREDICATE, REPEAT, REST,
    SAME, SYMBOL, TEST_ANY, TYPE, USING, VALUE, MatchCompiler, make_matcher,
    repeat_capture)
from koskari.reader import Reader, source_str


pytestmark = pytest.mark.phase_infra


def normal(value):
    """
    A capture value with its boxes made visible, so two results compare
    by shape and not by identity
    """

    if isinstance(value, DatumFrame):
        return ("frame", {k: normal(v) for k, v in value.items()},
                normal(value.source), value.position)
    if isinstance(value, Datum):
        return ("datum", normal(value.datum), value.position)
    if isinstance(value, dict):
        return {k: normal(v) for k, v in value.items()}
    if isinstance(value, list):
        return [normal(v) for v in value]
    if isinstance(value, tuple):
        return tuple(normal(v) for v in value)
    return value


def read(text):
    return Reader().read(source_str(text, "<test>"))


class TestMatchFunction(unittest.TestCase):

    def setUp(self):
        self.base_ns = ModuleNamespace("matcher", {})


    def check(self, pattern, *subjects, externs=None):
        """
        Both entries over every subject; the bare function's answer
        must be the Matcher's, captures and misses alike
        """

        frozen = MatchCompiler().lower(pattern)
        matcher = make_matcher(pattern)
        fn = compile_match_function(frozen, self.base_ns)
        if externs:
            for name, value in externs.items():
                self.base_ns.store(name, value)
        results = []
        for subject in subjects:
            want = matcher.match(subject, **(externs or {}))
            got = fn(subject).demand()
            if not want.matched:
                self.assertIsNone(got, subject)
            else:
                self.assertIsNotNone(got, subject)
                self.assertEqual(normal(got), normal(want.captures), subject)
            results.append(got)
        return results


    def test_define_shape_over_source(self):
        pattern = MATCH(CALLEDBY(), SYMBOL("sym"), VALUE("expr"))
        hit, miss = self.check(
            pattern, read("(define x 1)"), read("(define 1 1)"))
        self.assertIsInstance(hit, DatumFrame)
        self.assertIsInstance(hit["sym"], Datum)
        self.assertEqual(hit["sym"].datum, symbol("x"))
        self.assertIsNotNone(hit["sym"].position)
        self.assertIsNone(miss)

        # Matcher.match refuses an improper subject before it matches;
        # the bare function treats it as a miss
        fn = compile_match_function(MatchCompiler().lower(pattern), self.base_ns)
        self.assertIsNone(fn(cons(symbol("define"), symbol("x"))).demand())


    def test_body_rest(self):
        pattern = MATCH(CALLEDBY(), BODY("body"))
        hit, empty = self.check(
            pattern, read("(begin 1 2 3)"), read("(begin)"))
        self.assertEqual(hit["body"].datum, read("(1 2 3)"))
        self.assertIsNone(empty)


    def test_rest_before_suffix_and_literal_keyword(self):
        pattern = MATCH(CALLEDBY(), REST("front"), LITERAL_KW("as"), SYMBOL("name"))
        self.check(
            pattern, read("(f 1 2 as: x)"), read("(f as: x)"),
            read("(f 1 2 as: 3)"), read("(f 1 2)"))


    def test_bindings_rows(self):
        pattern = MATCH(CALLEDBY(), BINDINGS("bindings"), BODY("body"))
        hit, grouped, empty, bad = self.check(
            pattern,
            read("(let [[x 1] [y 2]] x)"),
            read("(let [[x 1] (2 as: y)] x)"),
            read("(let [] 1)"),
            read("(let [[x]] x)"))
        rows = hit["bindings"]
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]["name"].datum, symbol("x"))
        self.assertEqual(rows[1]["expr"].datum, 2)
        self.assertIsNone(bad)


    def test_formals(self):
        pattern = MATCH(CALLEDBY(), FORMALS("formals"), BODY("body"))
        self.check(
            pattern,
            read("(lambda [a b] a)"),
            read("(lambda [a (b 2) *: rest **: kw] a)"),
            read("(lambda [] 1)"),
            read("(lambda a a)"))


    def test_using_oneof_at_root(self):
        pattern = MATCH(CALLEDBY(), USING("using"))
        self.check(
            pattern,
            read("(for-each [x xs])"),
            read("(for-each [(a b) xs])"),
            read("(for-each [xs as: x])"),
            read("(for-each xs)"))


    def test_repeat_capture_rows_and_spans(self):
        pattern = MATCH(CALLEDBY(), repeat_capture("rows", FORM(MATCH(VALUE("x"), VALUE("y")))))
        hit, zero, odd = self.check(
            pattern, read("(f (1 2) (3 4))"), read("(f)"), read("(f (1 2) (3))"))
        self.assertEqual([r["x"].datum for r in hit["rows"]], [1, 3])
        self.assertEqual(zero["rows"], [])
        self.assertIsNone(odd)

        pattern = MATCH(CALLEDBY(), repeat_capture("elems", TEST_ANY()))
        hit, = self.check(pattern, read("(f 1 2 3)"))
        self.assertEqual([e.datum for e in hit["elems"]], [1, 2, 3])


    def test_repeat_stops_before_suffix(self):
        pattern = MATCH(CALLEDBY(), repeat_capture("elems", TEST_ANY()), LITERAL_KW("end"))
        hit, miss = self.check(pattern, read("(f 1 2 end:)"), read("(f 1 2)"))
        self.assertEqual([e.datum for e in hit["elems"]], [1, 2])
        self.assertIsNone(miss)


    def test_repeat_minimum(self):
        pattern = MATCH(CALLEDBY(), CAPTURE("elems", REPEAT(TEST_ANY(), min_count=2)))
        self.check(pattern, read("(f 1)"), read("(f 1 2)"), read("(f 1 2 3)"))


    def test_optional_yields_to_suffix(self):
        pattern = MATCH(CALLEDBY(), OPTIONAL(LITERAL_KW("opt")), VALUE("x"))
        self.check(pattern, read("(f opt: 1)"), read("(f 1)"), read("(f opt:)"))

        pattern = MATCH(CALLEDBY(), OPTIONAL(TEST_ANY()), VALUE("x"))
        self.check(pattern, read("(f 1)"), read("(f 1 2)"), read("(f)"))


    def test_same_and_not(self):
        pattern = MATCH(CALLEDBY(), VALUE("a"), SAME("a"), NOT(LITERAL(3)), TEST_ANY())
        self.check(pattern, read("(f 1 1 2)"), read("(f 1 2 2)"), read("(f 1 1 3)"))


    def test_wider_oneof_in_spine(self):
        pattern = MATCH(
            CALLEDBY(),
            ONEOF(MATCH(VALUE("a"), VALUE("b")), MATCH(VALUE("a"))),
            LITERAL_KW("end"))
        self.check(
            pattern, read("(f 1 2 end:)"), read("(f 1 end:)"), read("(f end:)"),
            read("(f 1 2 3 end:)"))


    def test_sequence_subjects_bind_raw(self):
        pattern = MATCH(VALUE("a"), CAPTURE("t", TYPE("int")), REST("r"))
        hit, miss = self.check(pattern, (1, 2, 3, 4), (1, "x"))
        self.assertIs(type(hit), dict)
        self.assertEqual(hit["r"], (3, 4))
        self.assertIsNone(miss)


    def test_core_types_read_no_namespace(self):
        pattern = MATCH(CAPTURE("k", TYPE("keyword")), CAPTURE("s", TYPE("symbol")))
        hit, = self.check(pattern, cons(keyword("a"), symbol("b"), nil))
        self.assertEqual(hit["s"].datum, symbol("b"))


    def test_extern_stored_into_the_namespace(self):
        pattern = MATCH(CAPTURE("x", TYPE("T")))
        self.check(pattern, (1,), ("s",), externs={"T": int})


    def test_datum_subject_cells_are_read_through(self):
        inner = read("(define x 1)")
        boxed = cons(Datum(symbol("define"), None), symbol("y"), 2, nil)
        pattern = MATCH(CALLEDBY(), SYMBOL("sym"), VALUE("expr"))
        self.check(pattern, inner, boxed)




class TestLiveSubjects(unittest.TestCase):
    """
    A list that user code shrinks during the match is read as it is at
    each step, and never past its end
    """

    def test_rest_after_the_list_is_cleared(self):
        subject = [1, 2, 3, 4]

        def clear(v):
            subject.clear()
            return True

        pattern = MATCH(FORM(MATCH(PREDICATE("clear"), REST("r"))))
        result = make_matcher(pattern).match((subject,), clear=clear)
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["r"], ())


    def test_collect_and_end_after_the_list_is_cleared(self):
        subject = [1, 2, 3, 4]

        def clear(v):
            subject.clear()
            return True

        pattern = MATCH(FORM(MATCH(
            PREDICATE("clear"), repeat_capture("xs", TEST_ANY()))))
        result = make_matcher(pattern).match((subject,), clear=clear)
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["xs"], [])

        subject[:] = [1, 2, 3, 4]
        pattern = MATCH(FORM(MATCH(PREDICATE("clear"), TEST_ANY())))
        result = make_matcher(pattern).match((subject,), clear=clear)
        self.assertFalse(result.matched)


    def test_rest_with_a_suffix_after_the_list_shrinks(self):
        subject = [1, 2, 3, 4, 5]

        def trim(v):
            del subject[2:]
            return True

        pattern = MATCH(FORM(MATCH(PREDICATE("trim"), REST("r"), TEST_ANY())))
        result = make_matcher(pattern).match((subject,), trim=trim)
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["r"], ())


class TestPairSpineElements(unittest.TestCase):
    """
    A pair spine is source: an element pulled from it is boxed, user
    code reached from the walk receives the box, and the matcher's own
    tests read through it
    """

    def test_predicate_receives_the_datum(self):
        seen = []

        def note(v):
            seen.append(v)
            return v.datum == 5

        pattern = MATCH(FORM(MATCH(PREDICATE("note"))))
        subject = cons(cons(5, nil), nil)
        self.assertTrue(make_matcher(pattern).match(subject, note=note).matched)
        self.assertIsInstance(seen[0], Datum)
        self.assertEqual(seen[0].datum, 5)


    def test_class_pattern_reads_through_the_box(self):
        class Point:
            __match_args__ = ("x", "y")

            def __init__(self, x, y):
                self.x = x
                self.y = y

        pattern = MATCH(FORM(MATCH(CLASS(
            "Point", FIELD(CAPTURE("x")), FIELD(CAPTURE("y"), name="y")))))
        subject = cons(cons(Point(3, 4), nil), nil)
        result = make_matcher(pattern).match(subject, Point=Point)
        self.assertTrue(result.matched)
        self.assertEqual(
            (result.captures["x"], result.captures["y"]), (3, 4))
        self.assertFalse(
            make_matcher(pattern).match(cons(cons(7, nil), nil), Point=Point).matched)


    def test_rest_with_a_suffix_boxes_a_spliced_datum_once(self):
        pattern = MATCH(REST("r"), BIND("z"))
        subject = cons(Datum(1, None), 2, 3, nil)
        result = make_matcher(pattern).match(subject)
        self.assertTrue(result.matched)
        first = result.captures["r"][0]
        self.assertIsInstance(first, Datum)
        self.assertEqual(first.datum, 1)


# The end.
