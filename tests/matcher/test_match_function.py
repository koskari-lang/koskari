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

from koskari.lib import (
    Datum, DatumFrame, ModuleNamespace, cons, keyword, nil, setcdr, symbol)
from koskari.match_code import (
    FunctionTarget, MatchLowering, compile_match_function)
from koskari.matcher import (
    APP, BIND, BINDINGS, BODY, CALLEDBY, CAPTURE, CLASS, FIELD, FORM, FORMALS,
    GROUP, LITERAL, LITERAL_KW, MATCH, NOT, ONEOF, OPTIONAL, PREDICATE, REPEAT,
    REST, SAME, SYMBOL, TEST_ANY, TYPE, USING, VALUE, MatchCompiler,
    make_matcher, repeat_capture)
from koskari.reader import Reader, source_str


pytestmark = pytest.mark.phase_infra


def read(text):
    return Reader().read(source_str(text, "<test>"))


class TestMatchFunction(unittest.TestCase):

    def setUp(self):
        self.base_ns = ModuleNamespace("matcher", {})


    def test_define_shape_over_source(self):
        pattern = MATCH(CALLEDBY(), SYMBOL("sym"), VALUE("expr"))
        m = make_matcher(pattern)
        hit = m.match(read("(define x 1)"))
        self.assertTrue(hit.matched)
        caps = hit.captures
        self.assertIsInstance(caps, DatumFrame)
        self.assertIsInstance(caps["sym"], Datum)
        self.assertEqual(caps["sym"].datum, symbol("x"))
        self.assertIsNotNone(caps["sym"].position)
        self.assertFalse(m.match(read("(define 1 1)")).matched)

        # Matcher.match refuses an improper subject before it matches;
        # the bare function treats it as a miss
        fn = compile_match_function(MatchCompiler().lower(pattern), self.base_ns)
        self.assertIsNone(fn(cons(symbol("define"), symbol("x"))).demand())


    def test_body_rest(self):
        m = make_matcher(MATCH(CALLEDBY(), BODY("body")))
        hit = m.match(read("(begin 1 2 3)"))
        self.assertTrue(hit.matched)
        self.assertEqual(hit.captures["body"].datum, read("(1 2 3)"))
        self.assertFalse(m.match(read("(begin)")).matched)


    def test_rest_before_suffix_and_literal_keyword(self):
        m = make_matcher(MATCH(
            CALLEDBY(), REST("front"), LITERAL_KW("as"), SYMBOL("name")))

        hit = m.match(read("(f 1 2 as: x)"))
        self.assertTrue(hit.matched)
        caps = hit.captures
        self.assertEqual(caps["called-by"].datum, symbol("f"))
        self.assertIsInstance(caps["front"], tuple)
        self.assertEqual([d.datum for d in caps["front"]], [1, 2])
        self.assertEqual(caps["name"].datum, symbol("x"))

        hit = m.match(read("(f as: x)"))
        self.assertTrue(hit.matched)
        caps = hit.captures
        # an empty rest before a suffix is unpinned by the docs
        self.assertIs(caps["front"].datum, nil)
        self.assertEqual(caps["name"].datum, symbol("x"))

        self.assertFalse(m.match(read("(f 1 2 as: 3)")).matched)
        self.assertFalse(m.match(read("(f 1 2)")).matched)


    def test_bindings_rows(self):
        m = make_matcher(MATCH(CALLEDBY(), BINDINGS("bindings"), BODY("body")))
        hit = m.match(read("(let [[x 1] [y 2]] x)"))
        self.assertTrue(hit.matched)
        rows = hit.captures["bindings"]
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]["name"].datum, symbol("x"))
        self.assertEqual(rows[1]["expr"].datum, 2)
        self.assertTrue(m.match(read("(let [[x 1] (2 as: y)] x)")).matched)
        self.assertTrue(m.match(read("(let [] 1)")).matched)
        self.assertFalse(m.match(read("(let [[x]] x)")).matched)


    def test_formals(self):
        m = make_matcher(MATCH(CALLEDBY(), FORMALS("formals"), BODY("body")))

        hit = m.match(read("(lambda [a b] a)"))
        self.assertTrue(hit.matched)
        formals = hit.captures["formals"]
        self.assertEqual(
            [p["name"].datum for p in formals["positionals"]],
            [symbol("a"), symbol("b")])
        self.assertEqual(formals["defaults"], [])
        self.assertEqual(formals["kwonly"], [])
        self.assertNotIn("star", formals)
        self.assertNotIn("starstar", formals)
        self.assertEqual(hit.captures["body"].datum, read("(a)"))

        hit = m.match(read("(lambda [a (b 2) *: rest **: kw] a)"))
        self.assertTrue(hit.matched)
        formals = hit.captures["formals"]
        first, second = formals["positionals"]
        self.assertEqual(first["name"].datum, symbol("a"))
        self.assertNotIn("default", first)
        self.assertEqual(second["name"].datum, symbol("b"))
        self.assertEqual(second["default"].datum, 2)
        self.assertEqual(formals["defaults"], [])
        self.assertEqual(formals["kwonly"], [])
        self.assertEqual(formals["star"].datum, symbol("rest"))
        self.assertEqual(formals["starstar"].datum, symbol("kw"))

        hit = m.match(read("(lambda [] 1)"))
        self.assertTrue(hit.matched)
        formals = hit.captures["formals"]
        self.assertEqual(formals["positionals"], [])
        self.assertEqual(formals["defaults"], [])
        self.assertEqual(formals["kwonly"], [])
        self.assertEqual(hit.captures["body"].datum, read("(1)"))

        self.assertFalse(m.match(read("(lambda a a)")).matched)


    def test_using_oneof_at_root(self):
        m = make_matcher(MATCH(CALLEDBY(), USING("using")))

        hit = m.match(read("(for-each [x xs])"))
        self.assertTrue(hit.matched)
        using = hit.captures["using"]
        self.assertEqual(using["name"].datum, symbol("x"))
        self.assertEqual(using["expr"].datum, symbol("xs"))
        self.assertNotIn("lhs", using)

        hit = m.match(read("(for-each [(a b) xs])"))
        self.assertTrue(hit.matched)
        using = hit.captures["using"]
        self.assertEqual(using["lhs"].datum, read("(a b)"))
        self.assertEqual(using["expr"].datum, symbol("xs"))
        self.assertNotIn("name", using)

        hit = m.match(read("(for-each [xs as: x])"))
        self.assertTrue(hit.matched)
        using = hit.captures["using"]
        self.assertEqual(using["expr"].datum, symbol("xs"))
        self.assertEqual(using["name"].datum, symbol("x"))
        self.assertNotIn("lhs", using)

        self.assertFalse(m.match(read("(for-each xs)")).matched)


    def test_repeat_capture_rows_and_spans(self):
        m = make_matcher(MATCH(
            CALLEDBY(),
            repeat_capture("rows", FORM(MATCH(VALUE("x"), VALUE("y"))))))
        hit = m.match(read("(f (1 2) (3 4))"))
        self.assertTrue(hit.matched)
        self.assertEqual([r["x"].datum for r in hit.captures["rows"]], [1, 3])
        zero = m.match(read("(f)"))
        self.assertTrue(zero.matched)
        self.assertEqual(zero.captures["rows"], [])
        self.assertFalse(m.match(read("(f (1 2) (3))")).matched)

        m = make_matcher(MATCH(CALLEDBY(), repeat_capture("elems", TEST_ANY())))
        hit = m.match(read("(f 1 2 3)"))
        self.assertTrue(hit.matched)
        self.assertEqual([e.datum for e in hit.captures["elems"]], [1, 2, 3])


    def test_repeat_stops_before_suffix(self):
        m = make_matcher(MATCH(
            CALLEDBY(), repeat_capture("elems", TEST_ANY()), LITERAL_KW("end")))
        hit = m.match(read("(f 1 2 end:)"))
        self.assertTrue(hit.matched)
        self.assertEqual([e.datum for e in hit.captures["elems"]], [1, 2])
        self.assertFalse(m.match(read("(f 1 2)")).matched)


    def test_repeat_minimum(self):
        m = make_matcher(MATCH(
            CALLEDBY(), CAPTURE("elems", REPEAT(TEST_ANY(), min_count=2))))

        self.assertFalse(m.match(read("(f 1)")).matched)

        hit = m.match(read("(f 1 2)"))
        self.assertTrue(hit.matched)
        self.assertIsInstance(hit.captures["elems"], list)
        self.assertEqual([e.datum for e in hit.captures["elems"]], [1, 2])

        hit = m.match(read("(f 1 2 3)"))
        self.assertTrue(hit.matched)
        self.assertEqual([e.datum for e in hit.captures["elems"]], [1, 2, 3])


    def test_optional_yields_to_suffix(self):
        m = make_matcher(MATCH(
            CALLEDBY(), OPTIONAL(LITERAL_KW("opt")), VALUE("x")))

        hit = m.match(read("(f opt: 1)"))
        self.assertTrue(hit.matched)
        self.assertEqual(set(hit.captures), {"called-by", "x"})
        self.assertEqual(hit.captures["x"].datum, 1)

        hit = m.match(read("(f 1)"))
        self.assertTrue(hit.matched)
        self.assertEqual(hit.captures["x"].datum, 1)

        hit = m.match(read("(f opt:)"))
        self.assertTrue(hit.matched)
        self.assertEqual(hit.captures["x"].datum, keyword("opt"))

        m = make_matcher(MATCH(CALLEDBY(), OPTIONAL(TEST_ANY()), VALUE("x")))

        hit = m.match(read("(f 1)"))
        self.assertTrue(hit.matched)
        self.assertEqual(hit.captures["x"].datum, 1)

        hit = m.match(read("(f 1 2)"))
        self.assertTrue(hit.matched)
        self.assertEqual(hit.captures["x"].datum, 2)

        self.assertFalse(m.match(read("(f)")).matched)


    def test_same_and_not(self):
        m = make_matcher(MATCH(
            CALLEDBY(), VALUE("a"), SAME("a"), NOT(LITERAL(3)), TEST_ANY()))

        hit = m.match(read("(f 1 1 2)"))
        self.assertTrue(hit.matched)
        self.assertEqual(set(hit.captures), {"called-by", "a"})
        self.assertEqual(hit.captures["a"].datum, 1)

        self.assertFalse(m.match(read("(f 1 2 2)")).matched)
        self.assertFalse(m.match(read("(f 1 1 3)")).matched)


    def test_wider_oneof_in_spine(self):
        m = make_matcher(MATCH(
            CALLEDBY(),
            ONEOF(MATCH(VALUE("a"), VALUE("b")), MATCH(VALUE("a"))),
            LITERAL_KW("end")))

        hit = m.match(read("(f 1 2 end:)"))
        self.assertTrue(hit.matched)
        self.assertEqual(hit.captures["a"].datum, 1)
        self.assertEqual(hit.captures["b"].datum, 2)

        hit = m.match(read("(f 1 end:)"))
        self.assertTrue(hit.matched)
        self.assertEqual(hit.captures["a"].datum, 1)
        self.assertNotIn("b", hit.captures)

        self.assertFalse(m.match(read("(f end:)")).matched)
        self.assertFalse(m.match(read("(f 1 2 3 end:)")).matched)


    def test_sequence_subjects_bind_raw(self):
        m = make_matcher(MATCH(VALUE("a"), CAPTURE("t", TYPE("int")), REST("r")))
        hit = m.match((1, 2, 3, 4))
        self.assertTrue(hit.matched)
        self.assertIs(type(hit.captures), dict)
        self.assertEqual(hit.captures["r"], (3, 4))
        self.assertFalse(m.match((1, "x")).matched)


    def test_core_types_read_no_namespace(self):
        m = make_matcher(MATCH(
            CAPTURE("k", TYPE("keyword")), CAPTURE("s", TYPE("symbol"))))
        hit = m.match(cons(keyword("a"), symbol("b"), nil))
        self.assertTrue(hit.matched)
        self.assertEqual(hit.captures["s"].datum, symbol("b"))


    def test_extern_stored_into_the_namespace(self):
        m = make_matcher(MATCH(CAPTURE("x", TYPE("T"))), {"T": int})

        hit = m.match((1,))
        self.assertTrue(hit.matched)
        self.assertIs(type(hit.captures), dict)
        self.assertEqual(hit.captures, {"x": 1})

        self.assertFalse(m.match(("s",)).matched)


    def test_datum_subject_cells_are_read_through(self):
        m = make_matcher(MATCH(CALLEDBY(), SYMBOL("sym"), VALUE("expr")))

        hit = m.match(read("(define x 1)"))
        self.assertTrue(hit.matched)
        self.assertEqual(hit.captures["called-by"].datum, symbol("define"))
        self.assertEqual(hit.captures["sym"].datum, symbol("x"))
        self.assertEqual(hit.captures["expr"].datum, 1)

        boxed = cons(Datum(symbol("define"), None), symbol("y"), 2, nil)
        hit = m.match(boxed)
        self.assertTrue(hit.matched)
        self.assertIs(hit.captures["called-by"].datum, symbol("define"))
        self.assertEqual(hit.captures["sym"].datum, symbol("y"))
        self.assertEqual(hit.captures["expr"].datum, 2)




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
        result = make_matcher(pattern, {"clear": clear}).match((subject,))
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["r"], ())


    def test_collect_and_end_after_the_list_is_cleared(self):
        subject = [1, 2, 3, 4]

        def clear(v):
            subject.clear()
            return True

        pattern = MATCH(FORM(MATCH(
            PREDICATE("clear"), repeat_capture("xs", TEST_ANY()))))
        result = make_matcher(pattern, {"clear": clear}).match((subject,))
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["xs"], [])

        subject[:] = [1, 2, 3, 4]
        pattern = MATCH(FORM(MATCH(PREDICATE("clear"), TEST_ANY())))
        result = make_matcher(pattern, {"clear": clear}).match((subject,))
        self.assertFalse(result.matched)


    def test_rest_with_a_suffix_after_the_list_shrinks(self):
        subject = [1, 2, 3, 4, 5]

        def trim(v):
            del subject[2:]
            return True

        pattern = MATCH(FORM(MATCH(PREDICATE("trim"), REST("r"), TEST_ANY())))
        result = make_matcher(pattern, {"trim": trim}).match((subject,))
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["r"], ())


class TestLoopingSpines(unittest.TestCase):
    """
    A pair spine that loops is refused at the descent into it, nested
    as deep as it may be, instead of being walked without end
    """

    def test_nested_loop_is_refused(self):
        cyclic = cons(1, 2, nil)
        setcdr(cyclic.cdr, cyclic)
        pattern = MATCH(FORM(MATCH(CAPTURE("xs", REPEAT(TYPE("int"))))))
        with self.assertRaises(ValueError) as caught:
            make_matcher(pattern).match((cyclic,))
        self.assertIn("looping", str(caught.exception))

        pattern = MATCH(FORM(MATCH(REST("r"))))
        with self.assertRaises(ValueError):
            make_matcher(pattern).match((cyclic,))


class TestMutatedSpines(unittest.TestCase):
    """
    A predicate that rewrites the subject's spine under the walk is
    user code changing the subject: the captures over the span it
    broke raise ValueError, not an internal error
    """

    def test_spine_cut_between_a_mark_and_its_captures(self):
        subject = cons(1, 2, 3, nil)

        def chop(v):
            setcdr(subject, nil)
            return True

        pattern = MATCH(
            GROUP("g", PREDICATE("chop"), CAPTURE("y")), CAPTURE("z"))
        with self.assertRaises(ValueError) as caught:
            make_matcher(pattern, {"chop": chop}).match(subject)
        self.assertIn("changed", str(caught.exception))


class TestRepeatScopes(unittest.TestCase):
    """
    Every pass of a repeat starts with nothing bound below it: a named
    group's slots that one pass bound and the next did not are not
    carried into the next pass's captures
    """

    def test_group_slots_do_not_carry_across_iterations(self):
        pattern = MATCH(repeat_capture("rows", FORM(MATCH(
            GROUP("g", OPTIONAL(CAPTURE("b", LITERAL(1)))), CAPTURE("c")))))
        result = make_matcher(pattern).match(((1, "x"), ("y",)))
        self.assertTrue(result.matched)
        self.assertEqual(
            result.captures["rows"],
            [{"g": {"b": 1}, "c": "x"}, {"g": {}, "c": "y"}])

        result = make_matcher(pattern).match(
            cons(cons(1, "x", nil), cons("y", nil), nil))
        rows = result.captures["rows"]
        self.assertEqual(list(rows[0]["g"].keys()), ["b"])
        self.assertEqual(list(rows[1]["g"].keys()), [])


class TestSameScopes(unittest.TestCase):
    """
    SAME names a capture of its own frame: a group's or repeat
    iteration's own binding at depth 0, the enclosing frame's at depth
    1, and a ONEOF branch shares its frame with what came before it
    """

    def test_same_in_a_repeat_reads_the_iteration_capture(self):
        pattern = MATCH(CAPTURE("x"), repeat_capture(
            "rows", FORM(MATCH(CAPTURE("x"), SAME("x")))))
        m = make_matcher(pattern)
        self.assertTrue(m.match((1, (2, 2), (3, 3))).matched)
        self.assertFalse(m.match((1, (2, 3))).matched)
        self.assertTrue(m.match((1, (1, 1))).matched)


    def test_same_at_depth_one_reads_the_enclosing_capture(self):
        pattern = MATCH(CAPTURE("x"), repeat_capture(
            "rows", FORM(MATCH(SAME("x", depth=1)))))
        m = make_matcher(pattern)
        self.assertTrue(m.match((1, (1,), (1,))).matched)
        self.assertFalse(m.match((1, (1,), (2,))).matched)


    def test_same_in_a_oneof_branch_shares_the_frame(self):
        pattern = MATCH(CAPTURE("x"), ONEOF(SAME("x"), LITERAL(0)))
        m = make_matcher(pattern)
        self.assertTrue(m.match((5, 5)).matched)
        self.assertTrue(m.match((5, 0)).matched)
        self.assertFalse(m.match((5, 6)).matched)


    def test_same_outside_its_frame_is_refused(self):
        pattern = MATCH(CAPTURE("x"), repeat_capture(
            "rows", FORM(MATCH(SAME("x")))))
        with self.assertRaises(ValueError) as caught:
            MatchCompiler().lower(pattern)
        self.assertIn("depth", str(caught.exception))


def temporaries(pattern):
    """
    The locals a standalone lowering declares beyond the subject and
    the captures: none, the cursors, marks and items living on the
    operand stack
    """

    frozen = MatchCompiler().lower(pattern)
    fn = compile_match_function(frozen, ModuleNamespace("probe", {}))
    names = [str(name).split("#")[0] for name in fn.code.namespace_spec.local_syms]
    return [name for name in names
            if name.startswith("match-") and name != "match-subject"]


class TestTemporaries(unittest.TestCase):
    """
    A lowering declares no local for the walk: the cursors, marks and
    items are entries on the operand stack for their extents, and the
    root's start is the subject itself
    """

    def test_a_flat_pattern_declares_no_temporary(self):
        self.assertEqual(temporaries(MATCH(LITERAL(1))), [])

    def test_a_bare_repeat_declares_no_temporary(self):
        self.assertEqual(
            temporaries(MATCH(FORM(MATCH(REPEAT(TYPE("int")))))), [])

    def test_a_repeat_over_a_group_declares_no_temporary(self):
        got = temporaries(MATCH(FORM(MATCH(
            repeat_capture("rows", GROUP("g", VALUE("k"), VALUE("v")))))))
        self.assertEqual(got, [])

    def test_bare_repeat_still_walks(self):
        m = MatchCompiler.compile(MATCH(FORM(MATCH(REPEAT(TYPE("int"))))))
        self.assertTrue(m.match(((1, 2, 3),)).matched)
        self.assertFalse(m.match(((1, "x"),)).matched)
        self.assertTrue(m.match((cons(1, 2, nil),)).matched)

    def test_boxed_subject_root_captures_keep_their_source(self):
        m = MatchCompiler.compile(MATCH(VALUE("x")))
        cell = cons(1, nil)
        result = m.match(Datum(cell))
        self.assertTrue(result.matched)
        self.assertIsInstance(result.captures, DatumFrame)
        self.assertIs(result.captures.source, cell)


def walk(pattern):
    """
    The instructions of a standalone lowering, each slot or temporary
    name shown without its gensym suffix
    """

    def plain(value):
        if isinstance(value, tuple):
            return tuple(plain(v) for v in value)
        if isinstance(value, symbol):
            return str(value).split("#")[0]
        return value

    frozen = MatchCompiler().lower(pattern)
    lowering = MatchLowering(FunctionTarget("t"), frozen, {}, None, None)
    lowering.lower_function()
    return [(op.name, plain(arg)) for op, arg in lowering.target.instructions]


def depth_trace(pattern, index=None):
    """
    The rows of a standalone lowering that name a cursor, mark or
    items entry, with the depth under the top of the stack each
    carries
    """

    frozen = MatchCompiler().lower(pattern)
    lowering = MatchLowering(FunctionTarget("t"), frozen, {}, None, None)
    if index is None:
        lowering.lower_function()
    else:
        lowering.lower_body("miss", index)
    return [(use[0].replace("_VAR", ""), dict(use[1]))
            for use in lowering.uses]


def rows(trace):
    return trace


class TestDepthTrace(unittest.TestCase):
    """
    The lowering traces, at every row that names a cursor, mark or
    items entry, the depth that entry sits at under the top of the
    stack: the subject under everything, a form's cursor above it,
    elements under test and marks above that
    """

    def test_flat_pattern(self):
        self.assertEqual(rows(depth_trace(MATCH(VALUE("x")))), [
            ("MATCH_NEXT_BIND", {"cur": 0}),
            ("MATCH_END", {"cur": 0}),
            ("MATCH_CAPTURES", {"cur": 0, "start": 1}),
        ])

    def test_nested_form_cursor_sits_above_the_outer(self):
        self.assertEqual(
            rows(depth_trace(MATCH(FORM(MATCH(VALUE("a"), LITERAL(2)))))), [
                ("MATCH_NEXT_FORM", {"cur": 0}),
                ("MATCH_NEXT_BIND", {"cur": 0}),
                ("MATCH_NEXT_LITERAL", {"cur": 0}),
                ("MATCH_END", {"cur": 0}),
                ("MATCH_END", {"cur": 0}),
                ("MATCH_CAPTURES", {"cur": 0, "start": 1}),
            ])

    def test_form_inside_an_application_result(self):
        # the pulled element, the call's result and the inner cursor
        # all sit above the outer cursor while the inner form walks
        trace = depth_trace(MATCH(APP("f", FORM(MATCH(VALUE("a"))))))
        self.assertEqual(rows(trace), [
            ("MATCH_NEXT", {"cur": 0}),
            ("MATCH_NEXT_BIND", {"cur": 0}),
            ("MATCH_END", {"cur": 0}),
            ("MATCH_END", {"cur": 0}),
            ("MATCH_CAPTURES", {"cur": 0, "start": 1}),
        ])

    def test_repeat_over_a_group(self):
        trace = depth_trace(MATCH(FORM(MATCH(
            repeat_capture("rows", GROUP("g", VALUE("k"), VALUE("v")))))))
        self.assertEqual(rows(trace), [
            ("MATCH_NEXT_FORM", {"cur": 0}),
            ("MATCH_MARK", {"cur": 1}),
            ("MATCH_REMARK", {"cur": 2, "mark": 0}),
            ("MATCH_NEXT_BIND", {"cur": 2}),
            ("MATCH_NEXT_BIND", {"cur": 2}),
            ("MATCH_CAPTURES", {"cur": 2, "start": 0}),
            ("MATCH_ITERATION", {"cur": 2, "items": 1, "start": 0}),
            ("MATCH_RESET", {"cur": 2, "mark": 0}),
            ("MATCH_END", {"cur": 0}),
            ("MATCH_END", {"cur": 0}),
            ("MATCH_CAPTURES", {"cur": 0, "start": 1}),
        ])

    def test_optional_reads_its_cursor_under_the_mark(self):
        trace = depth_trace(MATCH(FORM(MATCH(
            OPTIONAL(CAPTURE("o", LITERAL(1))), VALUE("z")))))
        self.assertEqual(rows(trace), [
            ("MATCH_NEXT_FORM", {"cur": 0}),
            ("MATCH_AVAILABLE", {"cur": 0}),
            ("MATCH_MARK", {"cur": 0}),
            ("MATCH_NEXT", {"cur": 1}),
            ("MATCH_RESET", {"cur": 1, "mark": 0}),
            ("MATCH_NEXT_BIND", {"cur": 0}),
            ("MATCH_END", {"cur": 0}),
            ("MATCH_END", {"cur": 0}),
            ("MATCH_CAPTURES", {"cur": 0, "start": 1}),
        ])

    def test_indexed_root_reads_captures_one_deeper(self):
        self.assertEqual(rows(depth_trace(MATCH(VALUE("x")), index=0))[-1],
                         ("MATCH_CAPTURES", {"cur": 1, "start": 2}))

    def test_trailing_oneof_branch_ends_its_form(self):
        # a ONEOF last in a form: every branch but the last tests the
        # form's end, its flag clear, on that branch's miss edge, so a
        # branch that leaves elements behind yields to the next
        pattern = MATCH(FORM(MATCH(ONEOF(
            MATCH(VALUE("a"), VALUE("b")), MATCH(VALUE("a"))))))
        self.assertEqual(walk(pattern), [
            ("VAR", "match-subject"),
            ("MATCH_FORM", ("match.miss_1", 257, 0, 0)),
            ("MATCH_NEXT_FORM", ("match.pop_2", 0, 258, 0)),
            ("MATCH_MARK", (0, 0, 0, 0)),
            ("MATCH_NEXT_BIND_VAR", ("match.or_5", 1, "a", 0)),
            ("MATCH_NEXT_BIND_VAR", ("match.or_5", 1, "b", 0)),
            ("MATCH_END", ("match.or_5", 1, 0, 0)),
            ("POP", 1),
            ("JUMP", "match.or_4"),
            ("LABEL", "match.or_5"),
            ("MATCH_RESET", (1, 0, 0, 0)),
            ("VAR_CLEAR", "a"),
            ("VAR_CLEAR", "b"),
            ("MATCH_NEXT_BIND_VAR", ("match.pop_6", 1, "a", 0)),
            ("POP", 1),
            ("LABEL", "match.or_4"),
            ("MATCH_END", ("match.pop_3", 0, 1, 0)),
            ("MATCH_END", ("match.pop_2", 0, 0, 0)),
            ("MATCH_CAPTURES", (("a", "b", "a"), "a", 1, 0)),
            ("RETURN", 1),
            ("LABEL", "match.pop_2"),
            ("POP", 1),
            ("JUMP", "match.miss_1"),
            ("LABEL", "match.pop_3"),
            ("POP", 2),
            ("JUMP", "match.miss_1"),
            ("LABEL", "match.pop_6"),
            ("POP", 3),
            ("JUMP", "match.miss_1"),
            ("LABEL", "match.miss_1"),
            ("CONST", None),
            ("RETURN", 1),
        ])


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
        self.assertTrue(make_matcher(pattern, {"note": note}).match(subject).matched)
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
        result = make_matcher(pattern, {"Point": Point}).match(subject)
        self.assertTrue(result.matched)
        self.assertEqual(
            (result.captures["x"], result.captures["y"]), (3, 4))
        self.assertFalse(
            make_matcher(pattern, {"Point": Point}).match(cons(cons(7, nil), nil)).matched)


    def test_rest_with_a_suffix_boxes_a_spliced_datum_once(self):
        pattern = MATCH(REST("r"), BIND("z"))
        subject = cons(Datum(1, None), 2, 3, nil)
        result = make_matcher(pattern).match(subject)
        self.assertTrue(result.matched)
        first = result.captures["r"][0]
        self.assertIsInstance(first, Datum)
        self.assertEqual(first.datum, 1)


# The end.
