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
Tests for the MATCH_* instructions: the pulls over pair and sequence
cursors kept on the operand stack and named by their depth under the
top, the peeks on the stacked element, the boxing rule, and the miss
edges leaving the cursor and the stack as they were.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import pytest

from koskari.lib import (
    CoreType, Datum, DatumFrame, OpCode, OpStyle, cons, gensym, nil, symbol)
from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


UNBOUNDED = 0xFF


def widths(mn, mx=UNBOUNDED):
    return (mn << 8) | mx


def counts(min_tail, suffix=0):
    return (min_tail << 8) | suffix


class Point:
    __match_args__ = ("x", "y")

    def __init__(self, x, y):
        self.x = x
        self.y = y


# the miss edge: the subject is still on the stack, under whatever
# the walk had pushed above it
MISS = [
    ["LABEL", "miss"],
    ["POP", 1],
    ["CONST", "miss"],
    ["RETURN", 1],
]


async def run(instrs, local_syms, values=None, subject=None):
    code, ns = build_expr(
        [["CONST", subject]] + instrs + MISS,
        local_syms=local_syms, values=values)
    return await make_callable(code, ns)()


class TestRows(CevalTest):

    def test_immediates_are_raw_fields(self):
        self.assertIs(OpCode.MATCH_FORM_FAST.extra[1], OpStyle.RAW)
        self.assertIs(OpCode.MATCH_NEXT_FORM_FAST.extra[2], OpStyle.RAW)
        self.assertIs(OpCode.MATCH_REST_BIND_FAST.extra[2], OpStyle.RAW)
        self.assertIs(OpCode.MATCH_CLASS_FAST.extra[2], OpStyle.RAW)
        self.assertIs(OpCode.MATCH_FIELD_POS_BIND_VAR.extra[2], OpStyle.RAW)
        for op in OpCode:
            if op.name.startswith("MATCH_") and op.name != "MATCH_CASE":
                self.assertNotIn(OpStyle.COUNT, op.extra, op.name)

    def test_cursor_fields_are_depths(self):
        # a cursor, mark or list is named by its depth under the top
        for op in (OpCode.MATCH_NEXT_FAST, OpCode.MATCH_NEXT_BIND_VAR,
                   OpCode.MATCH_END_FAST, OpCode.MATCH_REST_VAR,
                   OpCode.MATCH_NEXT_APPEND_FAST, OpCode.MATCH_COLLECT_VAR):
            self.assertIs(op.extra[1], OpStyle.RAW, op.name)
        self.assertIs(OpCode.MATCH_MARK_FAST.extra[0], OpStyle.RAW)
        self.assertEqual(
            OpCode.MATCH_RESET_VAR.extra[:2], (OpStyle.RAW, OpStyle.RAW))
        self.assertEqual(
            OpCode.MATCH_CAPTURES.extra,
            (OpStyle.CONST, OpStyle.FASTVAR, OpStyle.RAW, OpStyle.RAW))


class TestPulls(CevalTest):

    async def test_two_binds_over_pair_box_datums(self):
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(2, 2), 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 1, 0)],
            ["MATCH_END_FAST", ("miss", 0, 0, 0)],
            ["POP", 2],
            ["FAST", 0],
            ["FAST", 1],
            ["BUILD_ARGS", 2],
            ["RETURN", 1],
        ]
        a, b = await run(prog, ("a", "b"), subject=cons(1, 2, nil))
        self.assertIsInstance(a, Datum)
        self.assertIsInstance(b, Datum)
        self.assertEqual((a.datum, b.datum), (1, 2))


    async def test_two_binds_over_tuple_and_list_are_raw(self):
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(2, 2), 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 1, 0)],
            ["MATCH_END_FAST", ("miss", 0, 0, 0)],
            ["POP", 2],
            ["FAST", 0],
            ["FAST", 1],
            ["BUILD_ARGS", 2],
            ["RETURN", 1],
        ]
        for subject in ((1, 2), [1, 2]):
            got = await run(prog, ("a", "b"), subject=subject)
            self.assertEqual(got, (1, 2))


    async def test_form_pushes_the_cursor_above_the_subject(self):
        # the gate leaves the subject in place and the cursor on top;
        # the walk's end pops nothing with its flag clear, and pops the
        # cursor with it set
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(1, 1), 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 0, 0)],
            ["MATCH_END_FAST", ("miss", 0, 0, 0)],
            ["POP", 1],
            ["RETURN", 1],
        ]
        subject = (7,)
        self.assertIs(await run(prog, ("x",), subject=subject), subject)
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(1, 1), 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 0, 0)],
            ["MATCH_END_FAST", ("miss", 0, 1, 0)],
            ["RETURN", 1],
        ]
        self.assertIs(await run(prog, ("x",), subject=subject), subject)
        self.assertEqual(await run(prog, ("x",), subject=(7, 8)), "miss")


    async def test_length_precheck_and_end_miss(self):
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(2, 2), 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 1, 0)],
            ["MATCH_END_FAST", ("miss", 0, 0, 0)],
            ["POP", 2],
            ["CONST", "hit"],
            ["RETURN", 1],
        ]
        syms = ("a", "b")
        self.assertEqual(await run(prog, syms, subject=(1,)), "miss")
        self.assertEqual(await run(prog, syms, subject=(1, 2, 3)), "miss")
        self.assertEqual(
            await run(prog, syms, subject=cons(1, 2, 3, nil)), "miss")
        self.assertEqual(await run(prog, syms, subject=cons(1, nil)), "miss")
        # an improper tail is never at its end
        self.assertEqual(await run(prog, syms, subject=cons(1, 2)), "miss")
        self.assertEqual(await run(prog, syms, subject=(1, 2)), "hit")


    async def test_sequence_gate_rejects_atoms_and_mappings(self):
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(0), 0, 0)],
            ["POP", 2],
            ["CONST", "hit"],
            ["RETURN", 1],
        ]
        for subject in ("ab", b"ab", {"a": 1}, {1, 2}, 7):
            self.assertEqual(await run(prog, (), subject=subject), "miss")
        for subject in ((), [], nil, cons(1, nil)):
            self.assertEqual(await run(prog, (), subject=subject), "hit")


    async def test_rest_bind_tail_forms(self):
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(1), 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 0, 0)],
            ["MATCH_REST_BIND_FAST", ("miss", 0, counts(0), 1)],
            ["MATCH_END_FAST", ("miss", 0, 0, 0)],
            ["POP", 2],
            ["FAST", 1],
            ["RETURN", 1],
        ]
        syms = ("a", "r")
        got = await run(prog, syms, subject=cons(1, 2, 3, nil))
        self.assertIsInstance(got, Datum)
        self.assertEqual(got.datum, cons(2, 3, nil))

        got = await run(prog, syms, subject=cons(1, nil))
        self.assertIsInstance(got, Datum)
        self.assertIs(got.datum, nil)

        self.assertEqual(await run(prog, syms, subject=(1, 2, 3)), (2, 3))
        self.assertEqual(await run(prog, syms, subject=[1, 2, 3]), (2, 3))
        self.assertEqual(await run(prog, syms, subject=(1,)), ())


    async def test_rest_bind_before_suffix(self):
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(2), 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 0, 0)],
            ["MATCH_REST_BIND_FAST", ("miss", 0, counts(0, 1), 1)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 2, 0)],
            ["MATCH_END_FAST", ("miss", 0, 0, 0)],
            ["POP", 2],
            ["FAST", 0],
            ["FAST", 1],
            ["FAST", 2],
            ["BUILD_ARGS", 3],
            ["RETURN", 1],
        ]
        syms = ("a", "mid", "b")
        self.assertEqual(await run(prog, syms, subject=(1, 2)), (1, (), 2))
        self.assertEqual(
            await run(prog, syms, subject=(1, 2, 3)), (1, (2,), 3))

        a, mid, b = await run(prog, syms, subject=cons(1, 2, 3, 4, nil))
        self.assertEqual((a.datum, b.datum), (1, 4))
        self.assertEqual(tuple(d.datum for d in mid), (2, 3))

        a, mid, b = await run(prog, syms, subject=cons(1, 2, nil))
        self.assertIsInstance(mid, Datum)
        self.assertIs(mid.datum, nil)

        self.assertEqual(await run(prog, syms, subject=(1,)), "miss")


    async def test_rest_minimum_tail(self):
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(1), 0, 0)],
            ["MATCH_REST_FAST", ("miss", 0, counts(2), 0)],
            ["POP", 2],
            ["CONST", "hit"],
            ["RETURN", 1],
        ]
        self.assertEqual(await run(prog, (), subject=(1,)), "miss")
        self.assertEqual(await run(prog, (), subject=(1, 2)), "hit")
        self.assertEqual(await run(prog, (), subject=cons(1, nil)), "miss")
        self.assertEqual(await run(prog, (), subject=cons(1, 2, nil)), "hit")


    async def test_next_type_bind_miss_leaves_cursor(self):
        # a miss on the type test leaves the cursor where it was: the
        # retry from the miss label pulls the same element again
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(1, 1), 0, 0)],
            ["MATCH_NEXT_TYPE_BIND_FAST", ("retry", 0, 1, 0)],
            ["POP", 2],
            ["FAST", 0],
            ["RETURN", 1],
            ["LABEL", "retry"],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 0, 0)],
            ["POP", 2],
            ["FAST", 0],
            ["CONST", "retried"],
            ["BUILD_ARGS", 2],
            ["RETURN", 1],
        ]
        syms = ("x", "int")
        values = {"int": int}
        self.assertEqual(await run(prog, syms, values, subject=(7,)), 7)
        self.assertEqual(
            await run(prog, syms, values, subject=("s",)), ("s", "retried"))


    async def test_next_literal(self):
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(2, 2), 0, 0)],
            ["MATCH_NEXT_LITERAL_FAST", ("miss", 0, "k", 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 0, 0)],
            ["MATCH_END_FAST", ("miss", 0, 0, 0)],
            ["POP", 2],
            ["FAST", 0],
            ["RETURN", 1],
        ]
        self.assertEqual(await run(prog, ("v",), subject=("k", 9)), 9)
        self.assertEqual(await run(prog, ("v",), subject=("j", 9)), "miss")


    async def test_next_pushes_boxed_from_pair_and_raw_from_tuple(self):
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(1, 1), 0, 0)],
            ["MATCH_NEXT_FAST", ("miss", 0, 0, 0)],
            ["MATCH_BIND_FAST", 0],
            ["POP", 3],
            ["FAST", 0],
            ["RETURN", 1],
        ]
        got = await run(prog, ("x",), subject=cons(5, nil))
        self.assertIsInstance(got, Datum)
        self.assertEqual(got.datum, 5)
        self.assertEqual(await run(prog, ("x",), subject=(5,)), 5)


    async def test_pull_names_the_cursor_under_a_pushed_element(self):
        # with one element pushed, the cursor sits one deeper
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(2, 2), 0, 0)],
            ["MATCH_NEXT_FAST", ("miss", 0, 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 1, 0, 0)],
            ["MATCH_END_FAST", ("miss", 1, 0, 0)],
            ["MATCH_BIND_FAST", 1],
            ["POP", 3],
            ["FAST", 0],
            ["FAST", 1],
            ["BUILD_ARGS", 2],
            ["RETURN", 1],
        ]
        self.assertEqual(await run(prog, ("b", "a"), subject=(1, 2)), (2, 1))


    async def test_peek_does_not_advance(self):
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(1, 1), 0, 0)],
            ["MATCH_PEEK_FAST", ("miss", 0, 0, 0)],
            ["POP", 1],
            ["MATCH_PEEK_FAST", ("miss", 0, 0, 0)],
            ["MATCH_BIND_FAST", 0],
            ["POP", 1],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 1, 0)],
            ["MATCH_PEEK_FAST", ("end", 0, 0, 0)],
            ["POP", 3],
            ["CONST", "not at end"],
            ["RETURN", 1],
            ["LABEL", "end"],
            ["POP", 2],
            ["FAST", 0],
            ["FAST", 1],
            ["BUILD_ARGS", 2],
            ["RETURN", 1],
        ]
        syms = ("a", "b")
        self.assertEqual(await run(prog, syms, subject=(5,)), (5, 5))
        self.assertEqual(await run(prog, syms, subject=[5]), (5, 5))
        a, b = await run(prog, syms, subject=cons(5, nil))
        self.assertEqual((a.datum, b.datum), (5, 5))


    async def test_nested_form(self):
        # the inner cursor is pushed above the outer: the inner walk
        # names it at 0, and the outer is back at 0 once it is popped
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(2, 2), 0, 0)],
            ["MATCH_NEXT_FORM_FAST", ("miss", 0, widths(1, 1), 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 0, 0)],
            ["MATCH_END_FAST", ("miss", 0, 0, 0)],
            ["POP", 1],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 1, 0)],
            ["MATCH_END_FAST", ("miss", 0, 0, 0)],
            ["POP", 2],
            ["FAST", 0],
            ["FAST", 1],
            ["BUILD_ARGS", 2],
            ["RETURN", 1],
        ]
        syms = ("a", "b")
        self.assertEqual(await run(prog, syms, subject=((1,), 2)), (1, 2))
        self.assertEqual(await run(prog, syms, subject=([1], 2)), (1, 2))
        a, b = await run(prog, syms, subject=cons(cons(1, nil), 2, nil))
        self.assertEqual((a.datum, b.datum), (1, 2))
        self.assertEqual(await run(prog, syms, subject=((1, 1), 2)), "miss")
        self.assertEqual(await run(prog, syms, subject=(1, 2)), "miss")


    async def test_next_core_type_reads_no_slot(self):
        # a core type test carries its ordinal in the RAW field and
        # resolves nothing: a binding of the same name in the frame
        # is never read; a gensym passes SYMBOL and a bool passes INT
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(2, 2), 0, 0)],
            ["MATCH_NEXT_CORE_TYPE_BIND_FAST",
             ("miss", 0, int(CoreType.SYMBOL), 0)],
            ["MATCH_NEXT_CORE_TYPE_FAST", ("miss", 0, int(CoreType.INT), 0)],
            ["MATCH_END_FAST", ("miss", 0, 0, 0)],
            ["POP", 2],
            ["FAST", 0],
            ["RETURN", 1],
        ]
        syms = ("s", "symbol", "int")
        values = {"symbol": 5, "int": "not a type"}
        self.assertIs(
            await run(prog, syms, values, subject=(symbol("a"), 1)),
            symbol("a"))
        scoped = gensym(symbol("t"))
        self.assertIs(
            await run(prog, syms, values, subject=(scoped, True)), scoped)
        self.assertEqual(
            await run(prog, syms, values, subject=("a", 1)), "miss")
        self.assertEqual(
            await run(prog, syms, values, subject=(symbol("a"), 1.0)), "miss")
        self.assertEqual(
            await run(prog, syms, values, subject=(symbol("a"),)), "miss")
        # the pull reads a pair spine through the box and binds the box
        got = await run(prog, syms, values, subject=cons(symbol("a"), 1, nil))
        self.assertIsInstance(got, Datum)
        self.assertIs(got.datum, symbol("a"))


    async def test_next_core_type_miss_leaves_cursor(self):
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(1, 1), 0, 0)],
            ["MATCH_NEXT_CORE_TYPE_FAST", ("retry", 0, int(CoreType.STR), 0)],
            ["POP", 2],
            ["CONST", "str"],
            ["RETURN", 1],
            ["LABEL", "retry"],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 0, 0)],
            ["POP", 2],
            ["FAST", 0],
            ["RETURN", 1],
        ]
        self.assertEqual(await run(prog, ("x",), subject=("s",)), "str")
        self.assertEqual(await run(prog, ("x",), subject=(7,)), 7)
        self.assertEqual(await run(prog, ("x",), subject=[[1]]), [1])


    async def test_var_twins_store_by_name(self):
        prog = [
            ["MATCH_FORM_VAR", ("miss", widths(2, 2), 0, 0)],
            ["MATCH_NEXT_BIND_VAR", ("miss", 0, "a", 0)],
            ["MATCH_NEXT_TYPE_BIND_VAR", ("miss", 0, "int", "b")],
            ["MATCH_END_VAR", ("miss", 0, 0, 0)],
            ["POP", 2],
            ["VAR", "a"],
            ["VAR", "b"],
            ["BUILD_ARGS", 2],
            ["RETURN", 1],
        ]
        got = await run(prog, ("a", "b"), {"int": int}, subject=(1, 2))
        self.assertEqual(got, (1, 2))
        got = await run(prog, ("a", "b"), {"int": int}, subject=(1, "x"))
        self.assertEqual(got, "miss")


class TestPeeks(CevalTest):

    async def test_literal_and_type_bind_leave_tos(self):
        prog = [
            ["MATCH_LITERAL", ("miss", 7, 0, 0)],
            ["MATCH_TYPE_BIND_FAST", ("miss", 1, 0, 0)],
            ["FAST", 0],
            ["BUILD_ARGS", 2],
            ["RETURN", 1],
        ]
        syms = ("x", "int")
        values = {"int": int}
        self.assertEqual(await run(prog, syms, values, subject=7), (7, 7))
        self.assertEqual(await run(prog, syms, values, subject=8), "miss")
        self.assertEqual(await run(prog, syms, values, subject="7"), "miss")


    async def test_core_type_peek_and_bind(self):
        prog = [
            ["MATCH_CORE_TYPE", ("miss", int(CoreType.TUPLE), 0, 0)],
            ["MATCH_CORE_TYPE_BIND_FAST", ("miss", int(CoreType.TUPLE), 0, 0)],
            ["FAST", 0],
            ["BUILD_ARGS", 2],
            ["RETURN", 1],
        ]
        self.assertEqual(
            await run(prog, ("x",), subject=(1, 2)), ((1, 2), (1, 2)))
        self.assertEqual(await run(prog, ("x",), subject=[1, 2]), "miss")
        prog = [
            ["MATCH_CORE_TYPE_BIND_VAR", ("miss", int(CoreType.PAIR), "p", 0)],
            ["POP", 1],
            ["VAR", "p"],
            ["RETURN", 1],
        ]
        self.assertIs(await run(prog, ("p",), subject=nil), nil)
        self.assertEqual(await run(prog, ("p",), subject=(1,)), "miss")
        # an ordinal past the enum raises instead of matching
        prog = [
            ["MATCH_CORE_TYPE", ("miss", len(CoreType), 0, 0)],
            ["RETURN", 1],
        ]
        with self.assertRaises(SystemError):
            await run(prog, (), subject=1)


    async def test_type_miss_with_a_non_type_raises(self):
        prog = [
            ["MATCH_TYPE_FAST", ("miss", 0, 0, 0)],
            ["CONST", "hit"],
            ["RETURN", 1],
        ]
        with self.assertRaises(TypeError):
            await run(prog, ("t",), {"t": 7}, subject=7)


    async def test_class_and_fields(self):
        prog = [
            ["MATCH_CLASS_FAST", ("miss", 0, 2, 0)],
            ["MATCH_FIELD_POS_BIND_FAST", ("miss", 0, 0, 1)],
            ["MATCH_FIELD_POS_FAST", ("miss", 0, 1, 0)],
            ["MATCH_BIND_FAST", 2],
            ["POP", 1],
            ["MATCH_FIELD_KW_BIND_FAST", ("miss", "y", 3, 0)],
            ["MATCH_FIELD_KW", ("miss", "z", 0, 0)],
            ["POP", 1],
            ["POP", 1],
            ["FAST", 1],
            ["FAST", 2],
            ["FAST", 3],
            ["BUILD_ARGS", 3],
            ["RETURN", 1],
        ]
        syms = ("Point", "a", "b", "c")
        values = {"Point": Point}
        self.assertEqual(
            await run(prog, syms, values, subject=Point(3, 4)), "miss")
        p = Point(3, 4)
        p.z = 5
        self.assertEqual(await run(prog, syms, values, subject=p), (3, 4, 4))
        self.assertEqual(await run(prog, syms, values, subject=(3, 4)), "miss")


    async def test_class_without_match_args_raises(self):
        class Bare:
            pass

        prog = [
            ["MATCH_CLASS_FAST", ("miss", 0, 1, 0)],
            ["CONST", "hit"],
            ["RETURN", 1],
        ]
        with self.assertRaises(TypeError):
            await run(prog, ("Bare",), {"Bare": Bare}, subject=Bare())
        prog[0] = ["MATCH_CLASS_FAST", ("miss", 0, 0, 0)]
        self.assertEqual(
            await run(prog, ("Bare",), {"Bare": Bare}, subject=Bare()), "hit")


    async def test_map_and_items(self):
        prog = [
            ["MATCH_MAP", "miss"],
            ["MATCH_KEY_CONST_BIND_FAST", ("miss", "a", 1, 0)],
            ["MATCH_KEY_FAST_BIND_FAST", ("miss", 0, 2, 0)],
            ["MATCH_KEY_CONST", ("miss", "c", 0, 0)],
            ["POP", 1],
            ["POP", 1],
            ["FAST", 1],
            ["FAST", 2],
            ["BUILD_ARGS", 2],
            ["RETURN", 1],
        ]
        syms = ("key", "a", "b")
        values = {"key": "b"}
        got = await run(
            prog, syms, values, subject={"a": 1, "b": 2, "c": 3, "d": 4})
        self.assertEqual(got, (1, 2))
        self.assertEqual(
            await run(prog, syms, values, subject={"a": 1, "b": 2}), "miss")
        self.assertEqual(await run(prog, syms, values, subject=[1]), "miss")


    async def test_item_probe_does_not_insert(self):
        from collections import defaultdict

        prog = [
            ["MATCH_MAP", "miss"],
            ["MATCH_KEY_CONST", ("miss", "k", 0, 0)],
            ["POP", 1],
            ["POP", 1],
            ["CONST", "hit"],
            ["RETURN", 1],
        ]
        d = defaultdict(list)
        self.assertEqual(await run(prog, (), subject=d), "miss")
        self.assertEqual(dict(d), {})


class TestSameAndCollect(CevalTest):

    async def test_same_against_bound_and_unbound(self):
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(2, 2), 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 0, 0)],
            ["MATCH_NEXT_SAME_FAST", ("miss", 0, 0, 0)],
            ["MATCH_END_FAST", ("miss", 0, 0, 0)],
            ["POP", 2],
            ["FAST", 0],
            ["RETURN", 1],
        ]
        self.assertEqual(await run(prog, ("a",), subject=(4, 4)), 4)
        self.assertEqual(await run(prog, ("a",), subject=(4, 5)), "miss")
        got = await run(prog, ("a",), subject=cons(4, 4, nil))
        self.assertEqual(got.datum, 4)

        prog = [
            ["MATCH_SAME_FAST", ("miss", 0, 0, 0)],
            ["CONST", "hit"],
            ["RETURN", 1],
        ]
        # a slot never bound is a miss, not an error
        self.assertEqual(await run(prog, ("a",), subject=4), "miss")
        self.assertEqual(await run(prog, ("a",), {"a": 4}, subject=4), "hit")


    async def test_collect_and_append(self):
        # the items list is pushed above the cursor: the cursor is at
        # 1 and the list at 0 for the rows that read both
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(1), 0, 0)],
            ["MATCH_ITEMS", 0],
            ["MATCH_NEXT_APPEND_FAST", ("miss", 1, 0, 0)],
            ["MATCH_COLLECT_FAST", ("miss", 1, 0, 0)],
            ["MATCH_END_FAST", ("miss", 1, 0, 0)],
            ["RETURN", 1],
        ]
        self.assertEqual(await run(prog, (), subject=(1, 2, 3)), [1, 2, 3])
        self.assertEqual(await run(prog, (), subject=[1]), [1])
        got = await run(prog, (), subject=cons(1, 2, nil))
        self.assertEqual([d.datum for d in got], [1, 2])
        self.assertTrue(all(isinstance(d, Datum) for d in got))
        self.assertEqual(await run(prog, (), subject=cons(1, 2)), "miss")
        self.assertEqual(await run(prog, (), subject=()), "miss")


    async def test_count_against_the_items(self):
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(0), 0, 0)],
            ["MATCH_ITEMS", 0],
            ["MATCH_COUNT", ("short", 0, 1, 0)],
            ["CONST", "long enough"],
            ["RETURN", 1],
            ["LABEL", "short"],
            ["MATCH_NEXT_APPEND_FAST", ("miss", 1, 0, 0)],
            ["MATCH_COUNT", ("miss", 0, 1, 0)],
            ["CONST", "one appended"],
            ["RETURN", 1],
        ]
        self.assertEqual(await run(prog, (), subject=(1,)), "one appended")
        self.assertEqual(await run(prog, (), subject=()), "miss")


class TestMarkAndCaptures(CevalTest):

    async def test_mark_and_reset_on_both_cursors(self):
        # the mark is pushed above the cursor, so the cursor sits at 1
        # while it lives; a pull after the reset reads the marked
        # element again
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(2, 2), 0, 0)],
            ["MATCH_MARK_FAST", (0, 0, 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 1, 0, 0)],
            ["MATCH_RESET_FAST", (1, 0, 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 1, 1, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 1, 2, 0)],
            ["MATCH_END_FAST", ("miss", 1, 0, 0)],
            ["POP", 3],
            ["FAST", 0],
            ["FAST", 1],
            ["FAST", 2],
            ["BUILD_ARGS", 3],
            ["RETURN", 1],
        ]
        syms = ("a", "b", "c")
        self.assertEqual(await run(prog, syms, subject=(1, 2)), (1, 1, 2))
        self.assertEqual(await run(prog, syms, subject=[1, 2]), (1, 1, 2))
        a, b, c = await run(prog, syms, subject=cons(1, 2, nil))
        self.assertEqual((a.datum, b.datum, c.datum), (1, 1, 2))


    async def test_remark_moves_the_mark_to_the_cursor(self):
        # the mark pushed at 0, moved to the cursor after the first
        # pull; the reset returns to the moved position, so the second
        # element is read twice
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(2, 2), 0, 0)],
            ["MATCH_MARK_FAST", (0, 0, 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 1, 0, 0)],
            ["MATCH_REMARK_FAST", (1, 0, 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 1, 1, 0)],
            ["MATCH_RESET_FAST", (1, 0, 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 1, 2, 0)],
            ["MATCH_END_FAST", ("miss", 1, 0, 0)],
            ["POP", 3],
            ["FAST", 0],
            ["FAST", 1],
            ["FAST", 2],
            ["BUILD_ARGS", 3],
            ["RETURN", 1],
        ]
        syms = ("a", "b", "c")
        self.assertEqual(await run(prog, syms, subject=(1, 2)), (1, 2, 2))
        self.assertEqual(await run(prog, syms, subject=[1, 2]), (1, 2, 2))
        a, b, c = await run(prog, syms, subject=cons(1, 2, nil))
        self.assertEqual((a.datum, b.datum, c.datum), (1, 2, 2))


    async def test_a_mark_named_as_a_cursor_is_an_error(self):
        # over a sequence a mark and a cursor are distinct kinds, and a
        # depth that names the wrong one raises; a pair spine has one
        # kind, the cell, so the walk reads on from the mark
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(1, 1), 0, 0)],
            ["MATCH_MARK_FAST", (0, 0, 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 0, 0)],
            ["POP", 2],
            ["FAST", 0],
            ["RETURN", 1],
        ]
        with self.assertRaises(RuntimeError) as caught:
            await run(prog, ("a",), subject=(7,))
        self.assertIn("names a mark at depth 0", str(caught.exception))
        self.assertEqual((await run(prog, ("a",), subject=cons(7, nil))).datum, 7)

        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(1, 1), 0, 0)],
            ["MATCH_MARK_FAST", (0, 0, 0, 0)],
            ["MATCH_RESET_FAST", (1, 1, 0, 0)],
            ["POP", 2],
            ["CONST", "reset"],
            ["RETURN", 1],
        ]
        with self.assertRaises(RuntimeError) as caught:
            await run(prog, (), subject=(7,))
        self.assertIn("names a cursor at depth 1", str(caught.exception))
        self.assertEqual(await run(prog, (), subject=cons(7, nil)), "reset")


    async def test_a_foreign_mark_is_an_error(self):
        # over a sequence cursor the mark must be a mark over a
        # sequence: a list at the mark's depth raises, for the re-mark
        # and for the reset
        for op in ("MATCH_REMARK_FAST", "MATCH_RESET_FAST"):
            prog = [
                ["MATCH_FORM_FAST", ("miss", widths(0), 0, 0)],
                ["MATCH_NEXT_FAST", ("miss", 0, 0, 0)],
                ["POP", 1],
                ["MATCH_NEXT_FAST", ("miss", 0, 0, 0)],
                ["POP", 1],
                ["BUILD_LIST", 0],
                [op, (1, 0, 0, 0)],
                ["POP", 3],
                ["CONST", "moved"],
                ["RETURN", 1],
            ]
            with self.assertRaises(RuntimeError) as caught:
                await run(prog, (), subject=(1, 2, 3))
            self.assertIn("same cursor kind, not list", str(caught.exception))


    async def test_append_to_a_held_list_copies_it(self):
        # the items list duplicated on the stack stands for a cloned
        # segment holding it: the append and the collect write a copy
        # stored at their depth, and the held list keeps its contents
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(3, 3), 0, 0)],
            ["MATCH_ITEMS", 0],
            ["DUP", 1],
            ["MATCH_NEXT_APPEND_FAST", ("miss", 2, 0, 0)],
            ["MATCH_COLLECT_FAST", ("miss", 2, 0, 0)],
            ["MATCH_END_FAST", ("miss", 2, 0, 0)],
            ["BUILD_ARGS", 2],
            ["RETURN", 1],
        ]
        held, appended = await run(prog, (), subject=(1, 2, 3))
        self.assertEqual(held, [])
        self.assertEqual(appended, [1, 2, 3])
        self.assertIsNot(held, appended)

        held, appended = await run(prog, (), subject=cons(1, 2, 3, nil))
        self.assertEqual(held, [])
        self.assertEqual([d.datum for d in appended], [1, 2, 3])


    async def test_captures_over_pair_and_sequence(self):
        # the captures start at the subject, one under the cursor;
        # slots: a 0, b 1 (b never fires)
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(1, 1), 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 0, 0, 0)],
            ["MATCH_END_FAST", ("miss", 0, 0, 0)],
            ["MATCH_CAPTURES", (("a", "b"), 0, 1, 0)],
            ["RETURN", 1],
        ]
        syms = ("a", "b")
        got = await run(prog, syms, subject=(7,))
        self.assertIs(type(got), dict)
        self.assertEqual(got, {"a": 7})

        got = await run(prog, syms, subject=cons(7, nil))
        self.assertIsInstance(got, DatumFrame)
        self.assertEqual(list(got), ["a"])
        self.assertEqual(got["a"].datum, 7)
        self.assertEqual(got.source, cons(7, nil))


    async def test_captures_named_by_name(self):
        prog = [
            ["MATCH_FORM_VAR", ("miss", widths(1, 1), 0, 0)],
            ["MATCH_NEXT_BIND_VAR", ("miss", 0, "a", 0)],
            ["MATCH_END_VAR", ("miss", 0, 0, 0)],
            ["MATCH_CAPTURES_NAMED_VAR", ((("a", "b"), ("a", "b")), 1, 0, 0)],
            ["RETURN", 1],
        ]
        self.assertEqual(await run(prog, ("a", "b"), subject=(7,)), {"a": 7})


    async def test_iteration_appends_captures_or_span(self):
        # the items under the mark: a pass with one slot bound appends
        # its captures, a pass with none appends the span; the mark is
        # the top and the list under it when the iteration row runs
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(2, 2), 0, 0)],
            ["MATCH_ITEMS", 0],
            ["MATCH_MARK_FAST", (1, 0, 0, 0)],
            ["MATCH_NEXT_BIND_FAST", ("miss", 2, 0, 0)],
            ["MATCH_ITERATION", (("x",), 0, 2, 1)],
            ["POP", 1],
            ["MATCH_MARK_FAST", (1, 0, 0, 0)],
            ["MATCH_NEXT_FAST", ("miss", 2, 0, 0)],
            ["POP", 1],
            ["MATCH_ITERATION", (("x",), 0, 2, 1)],
            ["POP", 1],
            ["RETURN", 1],
        ]
        first, second = await run(prog, ("x",), subject=(1, 2))
        self.assertEqual(first, {"x": 1})
        self.assertEqual(second, 2)

        first, second = await run(prog, ("x",), subject=cons(1, 2, nil))
        self.assertIsInstance(first, DatumFrame)
        self.assertEqual(first["x"].datum, 1)
        self.assertEqual(first.source, 1)
        self.assertIsInstance(second, Datum)
        self.assertEqual(second.datum, 2)


    async def test_iteration_pushes_the_span_in_mode_zero(self):
        # an empty names tuple still names a first slot the frame has
        prog = [
            ["MATCH_FORM_FAST", ("miss", widths(1, 1), 0, 0)],
            ["MATCH_MARK_FAST", (0, 0, 0, 0)],
            ["MATCH_NEXT_FAST", ("miss", 1, 0, 0)],
            ["POP", 1],
            ["MATCH_ITERATION", ((), 0, 1, 0)],
            ["RETURN", 1],
        ]
        self.assertEqual(await run(prog, ("x",), subject=(9,)), 9)
        got = await run(prog, ("x",), subject=cons(9, nil))
        self.assertIsInstance(got, Datum)
        self.assertEqual(got.datum, 9)


# The end.
