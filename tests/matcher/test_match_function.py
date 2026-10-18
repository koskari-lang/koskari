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
for pair and sequence subjects, Datum and DatumFrame included, and the
locals it declares

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import (
    Datum, DatumFrame, ModuleNamespace, OpCode, cons, demand, keyword, nil,
    setcdr, symbol)
from koskari.match_code import compile_match_function
from koskari.match_core import analyse
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


    def test_same_outside_its_frame_always_misses(self):
        # the pass of a REPEAT over a group is a frame of its own, and
        # nothing in it binds x: the SAME builds and never matches
        pattern = MATCH(CAPTURE("x"), repeat_capture(
            "rows", FORM(MATCH(SAME("x")))))
        m = make_matcher(pattern)
        self.assertEqual(demand(m)((1,)), {"x": 1, "rows": []})
        self.assertIsNone(demand(m)((1, (1,))))
        self.assertIsNone(demand(m)((1, (2,))))
        self.assertIsNone(demand(m)((1, (1,), (1,))))


def local_names(pattern):
    """
    The locals a standalone lowering declares after the subject
    """

    frozen = MatchCompiler().lower(pattern)
    fn = compile_match_function(
        frozen, analyse(frozen), ModuleNamespace("probe", {}))
    return [str(name) for name in fn.code.namespace_spec.local_syms[1:]]


class TestTemporaries(unittest.TestCase):
    """
    A lowering declares no local beyond its subject formal: the
    cursors, marks and items are entries on the operand stack for
    their extents, and the captures live in the marks
    """

    def test_a_flat_pattern_declares_no_temporary(self):
        self.assertEqual(local_names(MATCH(LITERAL(1))), [])


    def test_a_bare_repeat_declares_no_temporary(self):
        self.assertEqual(
            local_names(MATCH(FORM(MATCH(REPEAT(TYPE("int")))))),
            [])


    def test_a_repeat_over_a_group_declares_no_temporary(self):
        pattern = MATCH(FORM(MATCH(
            repeat_capture("rows", GROUP("g", VALUE("k"), VALUE("v"))))))
        self.assertEqual(local_names(pattern), [])
        result = demand(pattern.build())(((1, 2, 3, 4),))
        self.assertEqual(
            result["rows"], [{"g": {"k": 1, "v": 2}}, {"g": {"k": 3, "v": 4}}])


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


class TestNestedMisses(unittest.TestCase):
    """
    A miss deep in the walk, under the entries the nodes above it
    pushed, gives None and leaves the matcher whole for the next call
    """

    def test_miss_inside_a_form_over_an_application_result(self):
        m = demand(make_matcher(
            MATCH(APP("f", FORM(MATCH(VALUE("a"))))), {"f": lambda v: v}))
        self.assertEqual(m(((1,),)), {"a": 1})
        self.assertIsNone(m(((1, 2),)))
        self.assertIsNone(m(((),)))
        self.assertIsNone(m((5,)))
        self.assertIsNone(m(((1,), 2)))
        self.assertEqual(m(((3,),)), {"a": 3})


    def test_miss_on_the_last_oneof_branch_length(self):
        m = demand(make_matcher(MATCH(FORM(MATCH(
            ONEOF(GROUP("g", VALUE("a"), VALUE("b")), VALUE("c")),
            LITERAL(9))))))
        self.assertEqual(m(((1, 2, 9),)), {"g": {"a": 1, "b": 2}})
        self.assertEqual(m(((1, 9),)), {"c": 1})
        self.assertIsNone(m(((9,),)))
        self.assertIsNone(m(((),)))
        self.assertIsNone(m(((1, 2, 3),)))
        self.assertEqual(m(((4, 9),)), {"c": 4})


    def test_trailing_oneof_branch_wider_first(self):
        # the wider branch is skipped by its length check when one
        # element remains, and the narrower branch takes it
        m = demand(make_matcher(MATCH(FORM(MATCH(ONEOF(
            MATCH(VALUE("a"), VALUE("b")), MATCH(VALUE("a"))))))))
        self.assertEqual(m(((1,),)), {"a": 1})
        self.assertEqual(m(((1, 2),)), {"a": 1, "b": 2})
        self.assertIsNone(m(((1, 2, 3),)))
        self.assertIsNone(m(((),)))


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


class TestCoreTwins(unittest.TestCase):
    """
    A built matcher's compiler lowers the matcher core's VAR
    instructions to their FAST twins, the extern a closure slot
    """

    def test_build_lowers_the_core_twins(self):
        class Point:
            __match_args__ = ("x",)

            def __init__(self, x):
                self.x = x

        pattern = MATCH(CLASS("Point", FIELD(CAPTURE("x"))))
        fn = make_matcher(pattern, {"Point": Point})
        ops = {OpCode(fn.code.oparg(i)[0]) for i in range(fn.code.length())}
        self.assertIn(OpCode.MATCH_CLASS_FAST, ops)
        self.assertIn(OpCode.MATCH_FIELD_POS_FAST, ops)
        self.assertNotIn(OpCode.MATCH_CLASS_VAR, ops)
        self.assertNotIn(OpCode.MATCH_FIELD_POS_VAR, ops)
        self.assertEqual(demand(fn)((Point(3),))["x"], 3)
        self.assertIsNone(demand(fn)((7,)))


class TestReentry(unittest.TestCase):
    """
    A matcher called again from its own walk, by a predicate or by a
    type's __instancecheck__, returns the captures of its own subject,
    and the interrupted walk then completes with its own
    """

    def test_reentered_from_its_predicate(self):
        inner = []
        holder = {}

        def p(v):
            if v == 1:
                inner.append(holder["m"]((5, 6)))
            return True

        m = demand(make_matcher(
            MATCH(CAPTURE("a", PREDICATE("p")), CAPTURE("b")), {"p": p}))
        holder["m"] = m
        self.assertEqual(m((1, 2)), {"a": 1, "b": 2})
        self.assertEqual(inner, [{"a": 5, "b": 6}])


    def test_reentered_from_an_instance_check(self):
        inner = []
        holder = {}

        class Meta(type):
            def __instancecheck__(cls, instance):
                if instance == 1:
                    inner.append(holder["m"]((7, 8)))
                return True

        class Kind(metaclass=Meta):
            pass

        m = demand(make_matcher(
            MATCH(CAPTURE("a", TYPE("Kind")), CAPTURE("b", TYPE("Kind"))),
            {"Kind": Kind}))
        holder["m"] = m
        self.assertEqual(m((1, 2)), {"a": 1, "b": 2})
        self.assertEqual(inner, [{"a": 7, "b": 8}])


# The end.
