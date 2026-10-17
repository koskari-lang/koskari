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
tests.matcher.test_match_function

The standalone matcher function: the captures and misses it returns
for pair and sequence subjects, Datum and DatumFrame included, the
depth trace and instructions of its lowering, and the temporaries it
declares

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import (
    Datum, DatumFrame, ModuleNamespace, cons, demand, keyword, nil, setcdr,
    symbol)
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
        hit = demand(m)(read("(define x 1)"))
        self.assertIsNotNone(hit)
        caps = hit
        self.assertIsInstance(caps, DatumFrame)
        self.assertIsInstance(caps["sym"], Datum)
        self.assertEqual(caps["sym"].datum, symbol("x"))
        self.assertIsNotNone(caps["sym"].position)
        self.assertIsNone(demand(m)(read("(define 1 1)")))

        # an improper subject is a miss
        self.assertIsNone(demand(m)(cons(symbol("define"), symbol("x"))))


    def test_body_rest(self):
        m = make_matcher(MATCH(CALLEDBY(), BODY("body")))
        hit = demand(m)(read("(begin 1 2 3)"))
        self.assertIsNotNone(hit)
        self.assertEqual(hit["body"].datum, read("(1 2 3)"))
        self.assertIsNone(demand(m)(read("(begin)")))


    def test_rest_before_suffix_and_literal_keyword(self):
        m = make_matcher(MATCH(
            CALLEDBY(), REST("front"), LITERAL_KW("as"), SYMBOL("name")))

        hit = demand(m)(read("(f 1 2 as: x)"))
        self.assertIsNotNone(hit)
        caps = hit
        self.assertEqual(caps["called-by"].datum, symbol("f"))
        self.assertIsInstance(caps["front"], tuple)
        self.assertEqual([d.datum for d in caps["front"]], [1, 2])
        self.assertEqual(caps["name"].datum, symbol("x"))

        hit = demand(m)(read("(f as: x)"))
        self.assertIsNotNone(hit)
        caps = hit
        # an empty rest before a suffix is unpinned by the docs
        self.assertIs(caps["front"].datum, nil)
        self.assertEqual(caps["name"].datum, symbol("x"))

        self.assertIsNone(demand(m)(read("(f 1 2 as: 3)")))
        self.assertIsNone(demand(m)(read("(f 1 2)")))


    def test_bindings_rows(self):
        m = make_matcher(MATCH(CALLEDBY(), BINDINGS("bindings"), BODY("body")))
        hit = demand(m)(read("(let [[x 1] [y 2]] x)"))
        self.assertIsNotNone(hit)
        rows = hit["bindings"]
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]["name"].datum, symbol("x"))
        self.assertEqual(rows[1]["expr"].datum, 2)
        self.assertIsNotNone(demand(m)(read("(let [[x 1] (2 as: y)] x)")))
        self.assertIsNotNone(demand(m)(read("(let [] 1)")))
        self.assertIsNone(demand(m)(read("(let [[x]] x)")))


    def test_formals(self):
        m = make_matcher(MATCH(CALLEDBY(), FORMALS("formals"), BODY("body")))

        hit = demand(m)(read("(lambda [a b] a)"))
        self.assertIsNotNone(hit)
        formals = hit["formals"]
        self.assertEqual(
            [p["name"].datum for p in formals["positionals"]],
            [symbol("a"), symbol("b")])
        self.assertEqual(formals["defaults"], [])
        self.assertEqual(formals["kwonly"], [])
        self.assertNotIn("star", formals)
        self.assertNotIn("starstar", formals)
        self.assertEqual(hit["body"].datum, read("(a)"))

        hit = demand(m)(read("(lambda [a (b 2) *: rest **: kw] a)"))
        self.assertIsNotNone(hit)
        formals = hit["formals"]
        first, second = formals["positionals"]
        self.assertEqual(first["name"].datum, symbol("a"))
        self.assertNotIn("default", first)
        self.assertEqual(second["name"].datum, symbol("b"))
        self.assertEqual(second["default"].datum, 2)
        self.assertEqual(formals["defaults"], [])
        self.assertEqual(formals["kwonly"], [])
        self.assertEqual(formals["star"].datum, symbol("rest"))
        self.assertEqual(formals["starstar"].datum, symbol("kw"))

        hit = demand(m)(read("(lambda [] 1)"))
        self.assertIsNotNone(hit)
        formals = hit["formals"]
        self.assertEqual(formals["positionals"], [])
        self.assertEqual(formals["defaults"], [])
        self.assertEqual(formals["kwonly"], [])
        self.assertEqual(hit["body"].datum, read("(1)"))

        self.assertIsNone(demand(m)(read("(lambda a a)")))


    def test_using_oneof_at_root(self):
        m = make_matcher(MATCH(CALLEDBY(), USING("using")))

        hit = demand(m)(read("(for-each [x xs])"))
        self.assertIsNotNone(hit)
        using = hit["using"]
        self.assertEqual(using["name"].datum, symbol("x"))
        self.assertEqual(using["expr"].datum, symbol("xs"))
        self.assertNotIn("lhs", using)

        hit = demand(m)(read("(for-each [(a b) xs])"))
        self.assertIsNotNone(hit)
        using = hit["using"]
        self.assertEqual(using["lhs"].datum, read("(a b)"))
        self.assertEqual(using["expr"].datum, symbol("xs"))
        self.assertNotIn("name", using)

        hit = demand(m)(read("(for-each [xs as: x])"))
        self.assertIsNotNone(hit)
        using = hit["using"]
        self.assertEqual(using["expr"].datum, symbol("xs"))
        self.assertEqual(using["name"].datum, symbol("x"))
        self.assertNotIn("lhs", using)

        self.assertIsNone(demand(m)(read("(for-each xs)")))


    def test_repeat_capture_rows_and_spans(self):
        m = make_matcher(MATCH(
            CALLEDBY(),
            repeat_capture("rows", FORM(MATCH(VALUE("x"), VALUE("y"))))))
        hit = demand(m)(read("(f (1 2) (3 4))"))
        self.assertIsNotNone(hit)
        self.assertEqual([r["x"].datum for r in hit["rows"]], [1, 3])
        zero = demand(m)(read("(f)"))
        self.assertIsNotNone(zero)
        self.assertEqual(zero["rows"], [])
        self.assertIsNone(demand(m)(read("(f (1 2) (3))")))

        m = make_matcher(MATCH(CALLEDBY(), repeat_capture("elems", TEST_ANY())))
        hit = demand(m)(read("(f 1 2 3)"))
        self.assertIsNotNone(hit)
        self.assertEqual([e.datum for e in hit["elems"]], [1, 2, 3])


    def test_repeat_stops_before_suffix(self):
        m = make_matcher(MATCH(
            CALLEDBY(), repeat_capture("elems", TEST_ANY()), LITERAL_KW("end")))
        hit = demand(m)(read("(f 1 2 end:)"))
        self.assertIsNotNone(hit)
        self.assertEqual([e.datum for e in hit["elems"]], [1, 2])
        self.assertIsNone(demand(m)(read("(f 1 2)")))


    def test_repeat_minimum(self):
        m = make_matcher(MATCH(
            CALLEDBY(), CAPTURE("elems", REPEAT(TEST_ANY(), min_count=2))))

        self.assertIsNone(demand(m)(read("(f 1)")))

        hit = demand(m)(read("(f 1 2)"))
        self.assertIsNotNone(hit)
        self.assertIsInstance(hit["elems"], list)
        self.assertEqual([e.datum for e in hit["elems"]], [1, 2])

        hit = demand(m)(read("(f 1 2 3)"))
        self.assertIsNotNone(hit)
        self.assertEqual([e.datum for e in hit["elems"]], [1, 2, 3])


    def test_optional_yields_to_suffix(self):
        m = make_matcher(MATCH(
            CALLEDBY(), OPTIONAL(LITERAL_KW("opt")), VALUE("x")))

        hit = demand(m)(read("(f opt: 1)"))
        self.assertIsNotNone(hit)
        self.assertEqual(set(hit), {"called-by", "x"})
        self.assertEqual(hit["x"].datum, 1)

        hit = demand(m)(read("(f 1)"))
        self.assertIsNotNone(hit)
        self.assertEqual(hit["x"].datum, 1)

        hit = demand(m)(read("(f opt:)"))
        self.assertIsNotNone(hit)
        self.assertEqual(hit["x"].datum, keyword("opt"))

        m = make_matcher(MATCH(CALLEDBY(), OPTIONAL(TEST_ANY()), VALUE("x")))

        hit = demand(m)(read("(f 1)"))
        self.assertIsNotNone(hit)
        self.assertEqual(hit["x"].datum, 1)

        hit = demand(m)(read("(f 1 2)"))
        self.assertIsNotNone(hit)
        self.assertEqual(hit["x"].datum, 2)

        self.assertIsNone(demand(m)(read("(f)")))


    def test_same_and_not(self):
        m = make_matcher(MATCH(
            CALLEDBY(), VALUE("a"), SAME("a"), NOT(LITERAL(3)), TEST_ANY()))

        hit = demand(m)(read("(f 1 1 2)"))
        self.assertIsNotNone(hit)
        self.assertEqual(set(hit), {"called-by", "a"})
        self.assertEqual(hit["a"].datum, 1)

        self.assertIsNone(demand(m)(read("(f 1 2 2)")))
        self.assertIsNone(demand(m)(read("(f 1 1 3)")))


    def test_wider_oneof_in_spine(self):
        m = make_matcher(MATCH(
            CALLEDBY(),
            ONEOF(MATCH(VALUE("a"), VALUE("b")), MATCH(VALUE("a"))),
            LITERAL_KW("end")))

        hit = demand(m)(read("(f 1 2 end:)"))
        self.assertIsNotNone(hit)
        self.assertEqual(hit["a"].datum, 1)
        self.assertEqual(hit["b"].datum, 2)

        hit = demand(m)(read("(f 1 end:)"))
        self.assertIsNotNone(hit)
        self.assertEqual(hit["a"].datum, 1)
        self.assertNotIn("b", hit)

        self.assertIsNone(demand(m)(read("(f end:)")))
        self.assertIsNone(demand(m)(read("(f 1 2 3 end:)")))


    def test_sequence_subjects_bind_raw(self):
        m = make_matcher(MATCH(VALUE("a"), CAPTURE("t", TYPE("int")), REST("r")))
        hit = demand(m)((1, 2, 3, 4))
        self.assertIsNotNone(hit)
        self.assertIs(type(hit), dict)
        self.assertEqual(hit["r"], (3, 4))
        self.assertIsNone(demand(m)((1, "x")))


    def test_core_types_read_no_namespace(self):
        m = make_matcher(MATCH(
            CAPTURE("k", TYPE("keyword")), CAPTURE("s", TYPE("symbol"))))
        hit = demand(m)(cons(keyword("a"), symbol("b"), nil))
        self.assertIsNotNone(hit)
        self.assertEqual(hit["s"].datum, symbol("b"))


    def test_extern_stored_into_the_namespace(self):
        m = make_matcher(MATCH(CAPTURE("x", TYPE("T"))), {"T": int})

        hit = demand(m)((1,))
        self.assertIsNotNone(hit)
        self.assertIs(type(hit), dict)
        self.assertEqual(hit, {"x": 1})

        self.assertIsNone(demand(m)(("s",)))


    def test_datum_subject_cells_are_read_through(self):
        m = make_matcher(MATCH(CALLEDBY(), SYMBOL("sym"), VALUE("expr")))

        hit = demand(m)(read("(define x 1)"))
        self.assertIsNotNone(hit)
        self.assertEqual(hit["called-by"].datum, symbol("define"))
        self.assertEqual(hit["sym"].datum, symbol("x"))
        self.assertEqual(hit["expr"].datum, 1)

        boxed = cons(Datum(symbol("define"), None), symbol("y"), 2, nil)
        hit = demand(m)(boxed)
        self.assertIsNotNone(hit)
        self.assertIs(hit["called-by"].datum, symbol("define"))
        self.assertEqual(hit["sym"].datum, symbol("y"))
        self.assertEqual(hit["expr"].datum, 2)




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
        result = demand(make_matcher(pattern, {"clear": clear}))((subject,))
        self.assertIsNotNone(result)
        self.assertEqual(result["r"], ())


    def test_collect_and_end_after_the_list_is_cleared(self):
        subject = [1, 2, 3, 4]

        def clear(v):
            subject.clear()
            return True

        pattern = MATCH(FORM(MATCH(
            PREDICATE("clear"), repeat_capture("xs", TEST_ANY()))))
        result = demand(make_matcher(pattern, {"clear": clear}))((subject,))
        self.assertIsNotNone(result)
        self.assertEqual(result["xs"], [])

        subject[:] = [1, 2, 3, 4]
        pattern = MATCH(FORM(MATCH(PREDICATE("clear"), TEST_ANY())))
        result = demand(make_matcher(pattern, {"clear": clear}))((subject,))
        self.assertIsNone(result)


    def test_rest_with_a_suffix_after_the_list_shrinks(self):
        subject = [1, 2, 3, 4, 5]

        def trim(v):
            del subject[2:]
            return True

        pattern = MATCH(FORM(MATCH(PREDICATE("trim"), REST("r"), TEST_ANY())))
        result = demand(make_matcher(pattern, {"trim": trim}))((subject,))
        self.assertIsNotNone(result)
        self.assertEqual(result["r"], ())


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
            demand(make_matcher(pattern))((cyclic,))
        self.assertIn("looping", str(caught.exception))

        pattern = MATCH(FORM(MATCH(REST("r"))))
        with self.assertRaises(ValueError):
            demand(make_matcher(pattern))((cyclic,))


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
            demand(make_matcher(pattern, {"chop": chop}))(subject)
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
        result = demand(make_matcher(pattern))(((1, "x"), ("y",)))
        self.assertIsNotNone(result)
        self.assertEqual(
            result["rows"],
            [{"g": {"b": 1}, "c": "x"}, {"g": {}, "c": "y"}])

        result = demand(make_matcher(pattern))(
            cons(cons(1, "x", nil), cons("y", nil), nil))
        rows = result["rows"]
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
        self.assertIsNotNone(demand(m)((1, (2, 2), (3, 3))))
        self.assertIsNone(demand(m)((1, (2, 3))))
        self.assertIsNotNone(demand(m)((1, (1, 1))))


    def test_same_at_depth_one_reads_the_enclosing_capture(self):
        pattern = MATCH(CAPTURE("x"), repeat_capture(
            "rows", FORM(MATCH(SAME("x", depth=1)))))
        m = make_matcher(pattern)
        self.assertIsNotNone(demand(m)((1, (1,), (1,))))
        self.assertIsNone(demand(m)((1, (1,), (2,))))


    def test_same_in_a_oneof_branch_shares_the_frame(self):
        pattern = MATCH(CAPTURE("x"), ONEOF(SAME("x"), LITERAL(0)))
        m = make_matcher(pattern)
        self.assertIsNotNone(demand(m)((5, 5)))
        self.assertIsNotNone(demand(m)((5, 0)))
        self.assertIsNone(demand(m)((5, 6)))


    def test_same_outside_its_frame_is_refused(self):
        pattern = MATCH(CAPTURE("x"), repeat_capture(
            "rows", FORM(MATCH(SAME("x")))))
        with self.assertRaises(ValueError) as caught:
            MatchCompiler().lower(pattern)
        self.assertIn("depth", str(caught.exception))


def local_names(pattern):
    """
    The locals a standalone lowering declares after the subject, each
    without its gensym suffix
    """

    frozen = MatchCompiler().lower(pattern)
    fn = compile_match_function(frozen, ModuleNamespace("probe", {}))
    return [str(name).split("#")[0]
            for name in fn.code.namespace_spec.local_syms[1:]]


class TestTemporaries(unittest.TestCase):
    """
    A lowering declares no local for the walk: the cursors, marks and
    items are entries on the operand stack for their extents, and the
    root's start is the subject itself
    """

    def test_a_flat_pattern_declares_no_temporary(self):
        self.assertEqual(local_names(MATCH(LITERAL(1))), [])


    def test_a_bare_repeat_declares_no_temporary(self):
        self.assertEqual(
            local_names(MATCH(FORM(MATCH(REPEAT(TYPE("int")))))),
            [])


    def test_a_repeat_over_a_group_declares_no_temporary(self):
        got = local_names(MATCH(FORM(MATCH(
            repeat_capture("rows", GROUP("g", VALUE("k"), VALUE("v")))))))
        self.assertEqual(got, ["rows", "g", "k", "v"])


    def test_bare_repeat_still_walks(self):
        m = MATCH(FORM(MATCH(REPEAT(TYPE("int"))))).build()
        self.assertIsNotNone(demand(m)(((1, 2, 3),)))
        self.assertIsNone(demand(m)(((1, "x"),)))
        self.assertIsNotNone(demand(m)((cons(1, 2, nil),)))


    def test_boxed_subject_root_captures_keep_their_source(self):
        m = MATCH(VALUE("x")).build()
        cell = cons(1, nil)
        result = demand(m)(Datum(cell))
        self.assertIsNotNone(result)
        self.assertIsInstance(result, DatumFrame)
        self.assertIs(result.source, cell)


def walk(pattern):
    """
    The instructions of a standalone lowering, the subject shown as
    "subject" and each slot or temporary name without its gensym suffix
    """

    def plain(value):
        if value is subject:
            return "subject"
        if isinstance(value, tuple):
            return tuple(plain(v) for v in value)
        if isinstance(value, symbol):
            return str(value).split("#")[0]
        return value

    frozen = MatchCompiler().lower(pattern)
    lowering = MatchLowering(FunctionTarget("t"), frozen, {}, None, None)
    subject = lowering.target.subject
    lowering.lower_function()
    return [(op.name, plain(arg)) for op, arg in lowering.target.instructions]


def after_label(instructions, label):
    """
    The two instructions that follow a label: a miss trampoline's pop
    and its jump
    """

    at = instructions.index(("LABEL", label))
    return instructions[at + 1:at + 3]


def depth_trace(pattern):
    """
    The rows of a standalone lowering that name a cursor, mark or
    items entry, with the depth under the top of the stack each
    carries
    """

    frozen = MatchCompiler().lower(pattern)
    lowering = MatchLowering(FunctionTarget("t"), frozen, {}, None, None)
    lowering.lower_function()
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


    def test_form_inside_an_application_misses_popping_four(self):
        # the outer cursor, the pulled element, the call's result and
        # the inner cursor are live when the inner form's pull misses
        instructions = walk(MATCH(APP("f", FORM(MATCH(VALUE("a"))))))
        binds = [arg for op, arg in instructions
                 if op == "MATCH_NEXT_BIND_VAR"]
        self.assertEqual(len(binds), 1)
        self.assertEqual(after_label(instructions, binds[0][0]), [
            ("POP", 4), ("JUMP", "match.miss_1")])


    def test_form_inside_an_application_emits_only_targeted_trampolines(self):
        # a trampoline is emitted only for a depth some miss edge jumps
        # from: none pops two, since no test misses with the outer
        # cursor and the pulled element alone above the subject
        self.assertEqual(walk(MATCH(APP("f", FORM(MATCH(VALUE("a")))))), [
            ("VAR", "subject"),
            ("MATCH_FORM", ("match.miss_1", 257, 0, 0)),
            ("MATCH_NEXT", ("match.pop_2", 0, 0, 0)),
            ("DUP", 1),
            ("CALL_VAR", ("f", 1, 0, 0)),
            ("MATCH_FORM", ("match.pop_4", 257, 0, 0)),
            ("MATCH_NEXT_BIND_VAR", ("match.pop_5", 0, "a", 0)),
            ("MATCH_END", ("match.pop_5", 0, 1, 0)),
            ("POP", 1),
            ("POP", 1),
            ("MATCH_END", ("match.pop_2", 0, 0, 0)),
            ("MATCH_CAPTURES", (("a",), "a", 1, 0)),
            ("RETURN", 1),
            ("LABEL", "match.pop_2"),
            ("POP", 1),
            ("JUMP", "match.miss_1"),
            ("LABEL", "match.pop_4"),
            ("POP", 3),
            ("JUMP", "match.miss_1"),
            ("LABEL", "match.pop_5"),
            ("POP", 4),
            ("JUMP", "match.miss_1"),
            ("LABEL", "match.miss_1"),
            ("CONST", None),
            ("RETURN", 1),
        ])


    def test_last_branch_guard_misses_with_the_mark_live(self):
        # the last branch's reservation check misses from under the
        # outer cursor, the inner cursor and the ONEOF's mark
        instructions = walk(MATCH(FORM(MATCH(
            ONEOF(GROUP("g", VALUE("a"), VALUE("b")), VALUE("c")),
            LITERAL(9)))))
        guards = [arg for op, arg in instructions if op == "MATCH_AVAILABLE"]
        self.assertEqual(len(guards), 2)
        self.assertEqual(after_label(instructions, guards[1][0]), [
            ("POP", 3), ("JUMP", "match.miss_1")])


    def test_trailing_oneof_branch_ends_its_form(self):
        # a ONEOF last in a form: every branch but the last tests the
        # form's end, its flag clear, on that branch's miss edge, so a
        # branch that leaves elements behind yields to the next
        pattern = MATCH(FORM(MATCH(ONEOF(
            MATCH(VALUE("a"), VALUE("b")), MATCH(VALUE("a"))))))
        self.assertEqual(walk(pattern), [
            ("VAR", "subject"),
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
        self.assertIsNotNone(demand(make_matcher(pattern, {"note": note}))(subject))
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
        result = demand(make_matcher(pattern, {"Point": Point}))(subject)
        self.assertIsNotNone(result)
        self.assertEqual(
            (result["x"], result["y"]), (3, 4))
        self.assertFalse(
            (demand(make_matcher(pattern, {"Point": Point}))(cons(cons(7, nil), nil)) is not None))


    def test_rest_with_a_suffix_boxes_a_spliced_datum_once(self):
        pattern = MATCH(REST("r"), BIND("z"))
        subject = cons(Datum(1, None), 2, 3, nil)
        result = demand(make_matcher(pattern))(subject)
        self.assertIsNotNone(result)
        first = result["r"][0]
        self.assertIsInstance(first, Datum)
        self.assertEqual(first.datum, 1)


# The end.
