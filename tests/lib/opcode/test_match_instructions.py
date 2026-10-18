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
tests.lib.opcode.test_match_instructions

Tests for the MATCH_* instructions: each one's stack
effect on a hit and on a miss, the cursors, marks and items lists they
name by depth, and the copy a write to one of them takes when another
stack entry holds it

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import pytest

from koskari.lib import (
    Code, CoreType, Datum, DatumFrame, NamespaceSpec, OpCode, OpStyle, cons,
    nil)
from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


UNBOUNDED = 0xFFFF

LABEL = OpStyle.LABEL
RAW = OpStyle.RAW
CONST = OpStyle.CONST
VAR = OpStyle.VAR
FASTVAR = OpStyle.FASTVAR
NOARG = OpStyle.NOARG


class Point:
    __match_args__ = ("x", "y")

    def __init__(self, x, y):
        self.x = x
        self.y = y


def ending(tag, depth):
    """
    The end of a path: tag pushed and returned in a tuple above the
    depth stack entries under it, the bottom one the base sentinel
    """

    return [
        ["CONST", tag],
        ["BUILD_ARGS", depth + 1],
        ["RETURN", 1],
    ]


def hit(depth):
    return ending("hit", depth)


def miss(depth, label="miss"):
    return [["LABEL", label]] + ending("miss", depth)


async def run(prog, subject=None, values=None, syms=()):
    """
    Run prog with "base" at the bottom of the stack and the subject
    above it
    """

    vals = {"subject": subject}
    vals.update(values or {})
    code, ns = build_expr(
        [["CONST", "base"], ["VAR", "subject"]] + prog,
        local_syms=tuple(syms), values=vals)
    return await make_callable(code, ns)()


class TestRows(CevalTest):

    def test_field_kinds(self):
        rows = {
            OpCode.MATCH_FITS: (LABEL, RAW, RAW, RAW),
            OpCode.MATCH_NEXT: (LABEL, RAW, NOARG, NOARG),
            OpCode.MATCH_BOUND: (LABEL, RAW, RAW, NOARG),
            OpCode.MATCH_UNBOUND: (RAW, NOARG, NOARG, NOARG),
            OpCode.MATCH_SYNC: (RAW, RAW, NOARG, NOARG),
            OpCode.MATCH_TAIL: (LABEL, RAW, RAW, RAW),
            OpCode.MATCH_SCOPE: (RAW, RAW, NOARG, NOARG),
            OpCode.MATCH_RESET: (RAW, RAW, NOARG, NOARG),
            OpCode.MATCH_AT: (RAW, NOARG, NOARG, NOARG),
            OpCode.MATCH_SPAN: (LABEL, RAW, RAW, NOARG),
            OpCode.MATCH_STORE: (RAW, RAW, RAW, CONST),
            OpCode.MATCH_CLEAR: (RAW, CONST, NOARG, NOARG),
            OpCode.MATCH_CAPTURES: (RAW, RAW, CONST, NOARG),
            OpCode.MATCH_PASS: (RAW, RAW, CONST, RAW),
            OpCode.MATCH_LOAD: (LABEL, RAW, RAW, NOARG),
            OpCode.MATCH_APPEND: (RAW, NOARG, NOARG, NOARG),
            OpCode.MATCH_MIN: (LABEL, RAW, RAW, NOARG),
            OpCode.MATCH_COLUMNS: (RAW, RAW, CONST, CONST),
            OpCode.MATCH_LITERAL: (LABEL, CONST, NOARG, NOARG),
            OpCode.MATCH_CORE: (LABEL, RAW, NOARG, NOARG),
            OpCode.MATCH_TYPE_VAR: (LABEL, VAR, NOARG, NOARG),
            OpCode.MATCH_TYPE_FAST: (LABEL, FASTVAR, NOARG, NOARG),
            OpCode.MATCH_CLASS_VAR: (LABEL, VAR, RAW, NOARG),
            OpCode.MATCH_CLASS_FAST: (LABEL, FASTVAR, RAW, NOARG),
            OpCode.MATCH_SAME: (LABEL, RAW, RAW, NOARG),
            OpCode.MATCH_FIELD_POS_VAR: (LABEL, VAR, RAW, NOARG),
            OpCode.MATCH_FIELD_POS_FAST: (LABEL, FASTVAR, RAW, NOARG),
            OpCode.MATCH_FIELD_KW: (LABEL, CONST, NOARG, NOARG),
            OpCode.MATCH_KEY: (LABEL, CONST, NOARG, NOARG),
            OpCode.MATCH_KEY_VAR: (LABEL, VAR, NOARG, NOARG),
            OpCode.MATCH_KEY_FAST: (LABEL, FASTVAR, NOARG, NOARG),
        }
        for op, extra in rows.items():
            self.assertIs(op.style, OpStyle.EXTRA, op.name)
            self.assertEqual(op.extra, extra, op.name)
        self.assertIs(OpCode.MATCH_DESCEND.style, OpStyle.LABEL)
        self.assertIs(OpCode.MATCH_MAPPING.style, OpStyle.LABEL)
        self.assertIs(OpCode.MATCH_SINGLE.style, OpStyle.NOARG)
        self.assertIs(OpCode.MATCH_ITEMS.style, OpStyle.NOARG)


    def test_store_index_must_name_a_slot(self):
        nspec = NamespaceSpec(None, (), ())
        with self.assertRaises(ValueError):
            Code(nspec, [[OpCode.MATCH_STORE, (1, 1, 0, ("a",))]], "t")
        with self.assertRaises(ValueError):
            Code(nspec, [[OpCode.MATCH_STORE, (1, 0, 0, "a")]], "t")
        with self.assertRaises(ValueError):
            Code(nspec, [[OpCode.MATCH_CLEAR, (0, 0, 0, 0)]], "t")
        with self.assertRaises(ValueError):
            Code(nspec, [[OpCode.MATCH_COLUMNS, (0, 1, ("a",), 0)]], "t")


class TestCursors(CevalTest):

    async def test_descend_replaces_the_subject(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["POP", 1],
        ] + hit(1) + miss(1)
        for subject in ((1, 2), [1, 2], cons(1, 2, nil), nil, ()):
            self.assertEqual(await run(prog, subject), ("base", "hit"))


    async def test_descend_miss_consumes_the_subject(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
        ] + hit(2) + miss(1)
        for subject in ("ab", {"a": 1}, 7, cons(1, 2), iter((1,))):
            self.assertEqual(
                await run(prog, subject), ("base", "miss"), subject)


    async def test_descend_reads_through_a_datum(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["MATCH_NEXT", ("miss", 0, 0, 0)],
        ] + hit(3) + miss(2)
        got = await run(prog, Datum((5,)))
        self.assertEqual((got[0], got[2], got[3]), ("base", 5, "hit"))


    async def test_single_walks_one_value(self):
        prog = [
            ["MATCH_SINGLE", 0],
            ["MATCH_NEXT", ("miss", 0, 0, 0)],
            ["MATCH_FITS", ("miss", 1, 0, 0)],
        ] + hit(3) + miss(2)
        subject = (1, 2)
        got = await run(prog, subject)
        self.assertIs(got[2], subject)
        self.assertEqual((got[0], got[3]), ("base", "hit"))


    async def test_fits(self):
        def prog(mn, mx):
            return [
                ["MATCH_DESCEND", "miss"],
                ["MATCH_FITS", ("miss", 0, mn, mx)],
            ] + hit(2) + miss(2)

        for subject in ((1, 2, 3), [1, 2, 3], cons(1, 2, 3, nil)):
            got = await run(prog(3, 3), subject)
            self.assertEqual((got[0], got[2]), ("base", "hit"))
            got = await run(prog(1, 2), subject)
            self.assertEqual((got[0], got[2]), ("base", "miss"))
            got = await run(prog(4, UNBOUNDED), subject)
            self.assertEqual(got[2], "miss")
            got = await run(prog(0, UNBOUNDED), subject)
            self.assertEqual(got[2], "hit")
            got = await run(prog(0, 0), subject)
            self.assertEqual(got[2], "miss")
        got = await run(prog(0, 0), ())
        self.assertEqual(got[2], "hit")


    async def test_next_boxes_spine_elements(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["MATCH_NEXT", ("miss", 0, 0, 0)],
            ["MATCH_NEXT", ("miss", 1, 0, 0)],
        ] + hit(4) + miss(0)
        got = await run(prog, (1, 2))
        self.assertEqual((got[0], got[2], got[3], got[4]),
                         ("base", 1, 2, "hit"))
        got = await run(prog, cons(1, 2, nil))
        self.assertIsInstance(got[2], Datum)
        self.assertEqual((got[2].datum, got[3].datum), (1, 2))


    async def test_next_miss_at_the_end_leaves_the_stack(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["MATCH_NEXT", ("miss", 0, 0, 0)],
            ["POP", 1],
            ["MATCH_NEXT", ("miss", 0, 0, 0)],
        ] + hit(3) + miss(2)
        for subject in ((1,), cons(1, nil)):
            got = await run(prog, subject)
            self.assertEqual((got[0], got[2]), ("base", "miss"))


    async def test_next_names_a_cursor(self):
        prog = [
            ["MATCH_NEXT", ("miss", 0, 0, 0)],
        ] + hit(2) + miss(1)
        with self.assertRaises(SystemError):
            await run(prog, (1, 2))


    async def test_bound_and_sync(self):
        # the REPEAT shape: a bound cursor that leaves one element for
        # the suffix, three pulls on it, its end, then the parent
        # synced to it and the last element pulled from the parent
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["MATCH_BOUND", ("miss", 0, 1, 0)],
            ["MATCH_NEXT", ("miss", 0, 0, 0)],
            ["MATCH_NEXT", ("miss", 1, 0, 0)],
            ["MATCH_NEXT", ("miss", 2, 0, 0)],
            ["BUILD_ARGS", 3],
            ["MATCH_NEXT", ("end", 1, 0, 0)],
            ["CONST", "bound end not reached"],
            ["RETURN", 1],
            ["LABEL", "end"],
            ["MATCH_SYNC", (2, 1, 0, 0)],
            ["MATCH_FITS", ("miss", 2, 1, 1)],
            ["MATCH_NEXT", ("miss", 2, 0, 0)],
        ] + hit(5) + miss(4)
        got = await run(prog, [1, 2, 3, 4])
        self.assertEqual(got[0], "base")
        self.assertEqual(got[3], (1, 2, 3))
        self.assertEqual(got[4:], (4, "hit"))
        got = await run(prog, cons(1, 2, 3, 4, nil))
        self.assertEqual([d.datum for d in got[3]], [1, 2, 3])
        self.assertEqual(got[4].datum, 4)


    async def test_bound_miss(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["MATCH_BOUND", ("miss", 0, 3, 0)],
        ] + hit(3) + miss(2)
        got = await run(prog, (1, 2))
        self.assertEqual((got[0], got[2]), ("base", "miss"))
        got = await run(prog, cons(1, 2, 3, nil))
        self.assertEqual((got[0], got[3]), ("base", "hit"))


    async def test_unbound_drops_the_reserve(self):
        # a bound cursor ending two early, one element pulled from it,
        # then its unbound copy: the copy's span runs to the subject's
        # end from the same position, and the bound cursor's span still
        # stops at its bound
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["MATCH_BOUND", ("miss", 0, 2, 0)],
            ["MATCH_NEXT", ("miss", 0, 0, 0)],
            ["POP", 1],
            ["MATCH_UNBOUND", (0, 0, 0, 0)],
            ["MATCH_TAIL", ("miss", 0, 0, 0)],
            ["MATCH_TAIL", ("miss", 2, 0, 0)],
        ] + hit(6) + miss(3)
        for subject in ((1, 2, 3, 4), [1, 2, 3, 4]):
            got = await run(prog, subject)
            self.assertEqual(got[0], "base")
            self.assertIsNot(got[3], got[2])
            self.assertEqual(got[4:], ((2, 3, 4), (2,), "hit"))

        spine = cons(1, 2, 3, 4, nil)
        got = await run(prog, spine)
        self.assertIsInstance(got[4], Datum)
        self.assertIs(got[4].datum, spine.cdr)
        self.assertEqual([d.datum for d in got[5]], [2])
        self.assertEqual(got[6], "hit")


    async def test_unbound_over_an_unbounded_cursor(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["MATCH_NEXT", ("miss", 0, 0, 0)],
            ["POP", 1],
            ["MATCH_UNBOUND", (0, 0, 0, 0)],
            ["MATCH_TAIL", ("miss", 0, 0, 0)],
            ["MATCH_TAIL", ("miss", 2, 0, 0)],
        ] + hit(5) + miss(2)
        got = await run(prog, (1, 2, 3))
        self.assertEqual(got[0], "base")
        self.assertIsNot(got[2], got[1])
        self.assertEqual(got[3:], ((2, 3), (2, 3), "hit"))

        prog = [
            ["MATCH_SINGLE", 0],
            ["MATCH_UNBOUND", (0, 0, 0, 0)],
            ["MATCH_NEXT", ("miss", 0, 0, 0)],
            ["MATCH_FITS", ("miss", 1, 0, 0)],
            ["MATCH_FITS", ("miss", 2, 1, 1)],
        ] + hit(4) + miss(3)
        got = await run(prog, "v")
        self.assertEqual((got[0], got[3], got[4]), ("base", "v", "hit"))


    async def test_tail_forms(self):
        def prog(w, mn):
            return [
                ["MATCH_DESCEND", "miss"],
                ["MATCH_NEXT", ("miss", 0, 0, 0)],
                ["POP", 1],
                ["MATCH_TAIL", ("miss", 0, w, mn)],
                ["MATCH_FITS", ("miss", 1, w, w)],
            ] + hit(3) + miss(2)

        # over a list or tuple, a tuple
        for subject in ((1, 2, 3, 4), [1, 2, 3, 4]):
            got = await run(prog(1, 0), subject)
            self.assertEqual(got, ("base", got[1], (2, 3), "hit"))

        # over a spine to its end, a Datum over the sub-spine
        spine = cons(1, 2, 3, nil)
        got = await run(prog(0, 0), spine)
        self.assertIsInstance(got[2], Datum)
        self.assertIs(got[2].datum, spine.cdr)

        # short of the end, a Datum per cell
        got = await run(prog(1, 0), spine)
        self.assertEqual([d.datum for d in got[2]], [2])

        # empty, a Datum over the terminator
        got = await run(prog(2, 0), spine)
        self.assertIs(got[2].datum, nil)

        # below the minimum, or fewer than the suffix, a miss
        got = await run(prog(1, 3), (1, 2, 3, 4))
        self.assertEqual((got[0], got[2]), ("base", "miss"))
        got = await run(prog(4, 0), (1, 2, 3, 4))
        self.assertEqual(got[2], "miss")


    async def test_tail_over_a_single_value(self):
        prog = [
            ["MATCH_SINGLE", 0],
            ["MATCH_TAIL", ("miss", 0, 0, 0)],
        ] + hit(3) + miss(2)
        got = await run(prog, "v")
        self.assertEqual((got[0], got[2], got[3]), ("base", ("v",), "hit"))


class TestMarks(CevalTest):

    async def test_scope_reset_at_span(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["MATCH_NEXT", ("miss", 0, 0, 0)],
            ["POP", 1],
            ["MATCH_SCOPE", (0, 0, 0, 0)],
            ["MATCH_SPAN", ("empty", 0, 1, 0)],
            ["CONST", "an empty span pushed"],
            ["RETURN", 1],
            ["LABEL", "empty"],
            ["MATCH_NEXT", ("miss", 1, 0, 0)],
            ["POP", 1],
            ["MATCH_SPAN", ("miss", 0, 1, 0)],
            ["MATCH_NEXT", ("miss", 2, 0, 0)],
            ["POP", 1],
            ["MATCH_SPAN", ("miss", 1, 2, 0)],
            ["MATCH_AT", (2, 0, 0, 0)],
            ["MATCH_RESET", (4, 3, 0, 0)],
            ["MATCH_NEXT", ("miss", 4, 0, 0)],
        ] + hit(7) + miss(0)
        got = await run(prog, (1, 2, 3, 4))
        self.assertEqual(got[0], "base")
        self.assertEqual(got[3:], (2, (2, 3), 2, 2, "hit"))

        got = await run(prog, cons(1, 2, 3, 4, nil))
        one, two, at, again = got[3:7]
        self.assertEqual(one.datum, 2)
        self.assertEqual([d.datum for d in two], [2, 3])
        self.assertEqual((at.datum, again.datum), (2, 2))


    async def test_store_binds_a_spine_datum(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["MATCH_SCOPE", (0, 2, 0, 0)],
            ["MATCH_NEXT", ("miss", 1, 0, 0)],
            ["MATCH_STORE", (1, 1, 2, ("a", "b"))],
            ["MATCH_CAPTURES", (0, 1, ("a", "b"), 0)],
        ] + hit(4) + miss(0)
        got = await run(prog, cons(5, nil))
        caps = got[3]
        self.assertIsInstance(caps, DatumFrame)
        self.assertEqual(list(caps), ["b"])
        self.assertEqual(caps["b"].datum, 5)
        self.assertEqual(caps["b"].binding, "b")


    async def test_store_leaves_a_user_datum(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["MATCH_SCOPE", (0, 1, 0, 0)],
            ["MATCH_NEXT", ("miss", 1, 0, 0)],
            ["MATCH_STORE", (1, 0, 2, ("a",))],
            ["MATCH_CAPTURES", (0, 1, ("a",), 0)],
        ] + hit(4) + miss(0)
        mine = Datum(5)
        got = await run(prog, (mine,))
        self.assertEqual(got[3], {"a": mine})
        self.assertIs(got[3]["a"], mine)
        self.assertIsNone(mine.binding)

        prog[0] = ["MATCH_SINGLE", 0]
        got = await run(prog, mine)
        self.assertIs(got[3]["a"], mine)
        self.assertIsNone(mine.binding)


    async def test_store_with_no_cursor_leaves_the_datum(self):
        # cursor depth 0 names no cursor: a spine element is stored as
        # it was, with no binding
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["MATCH_SCOPE", (0, 1, 0, 0)],
            ["MATCH_NEXT", ("miss", 1, 0, 0)],
            ["MATCH_STORE", (1, 0, 0, ("a",))],
            ["MATCH_CAPTURES", (0, 1, ("a",), 0)],
        ] + hit(4) + miss(0)
        got = await run(prog, cons(5, nil))
        self.assertEqual(list(got[3]), ["a"])
        self.assertEqual(got[3]["a"].datum, 5)
        self.assertIsNone(got[3]["a"].binding)


    async def test_store_consumes_the_value(self):
        prog = [
            ["MATCH_SINGLE", 0],
            ["MATCH_SCOPE", (0, 1, 0, 0)],
            ["CONST", 7],
            ["MATCH_STORE", (1, 0, 2, ("a",))],
        ] + hit(3)
        got = await run(prog, "v")
        self.assertEqual((got[0], got[3]), ("base", "hit"))


    async def test_clear_empties_slots(self):
        prog = [
            ["MATCH_SINGLE", 0],
            ["MATCH_SCOPE", (0, 3, 0, 0)],
            ["CONST", 1],
            ["MATCH_STORE", (1, 0, 2, ("a", "b", "c"))],
            ["CONST", 2],
            ["MATCH_STORE", (1, 1, 2, ("a", "b", "c"))],
            ["CONST", 3],
            ["MATCH_STORE", (1, 2, 2, ("a", "b", "c"))],
            ["MATCH_CLEAR", (0, (0, 2), 0, 0)],
            ["MATCH_CAPTURES", (0, 1, ("a", "b", "c"), 0)],
        ] + hit(4)
        got = await run(prog, "v")
        self.assertEqual(got[3], {"b": 2})
        self.assertIs(type(got[3]), dict)


    async def test_pass_is_the_dict_or_the_span(self):
        def prog(width, fill, group=0):
            body = [
                ["MATCH_DESCEND", "miss"],
                ["MATCH_SCOPE", (0, 1, 0, 0)],
            ]
            for _ in range(width):
                body += [["MATCH_NEXT", ("miss", 1, 0, 0)]]
                if fill:
                    body += [["MATCH_STORE", (1, 0, 2, ("x",))]]
                else:
                    body += [["POP", 1]]
            return body + [
                ["MATCH_PASS", (0, 1, ("x",), group)],
            ] + hit(4) + miss(0)

        self.assertEqual((await run(prog(1, True), (7,)))[3], {"x": 7})
        self.assertEqual((await run(prog(1, False), (7,)))[3], 7)
        self.assertEqual((await run(prog(2, False), (7, 8)))[3], (7, 8))
        got = (await run(prog(1, False), cons(7, nil)))[3]
        self.assertIsInstance(got, Datum)
        self.assertEqual(got.datum, 7)
        got = (await run(prog(1, True), cons(7, nil)))[3]
        self.assertIs(type(got), dict)
        self.assertEqual(got["x"].datum, 7)

        # with the group flag, the captures as MATCH_CAPTURES gives
        # them: a DatumFrame over a spine, a dict otherwise
        got = (await run(prog(1, True, 1), cons(7, nil)))[3]
        self.assertIsInstance(got, DatumFrame)
        self.assertEqual(got["x"].datum, 7)
        got = (await run(prog(1, True, 1), (7,)))[3]
        self.assertIs(type(got), dict)
        got = (await run(prog(1, False, 1), cons(7, nil)))[3]
        self.assertIsInstance(got, Datum)


    async def test_load_pushes_or_skips(self):
        prog = [
            ["MATCH_SINGLE", 0],
            ["MATCH_SCOPE", (0, 2, 0, 0)],
            ["CONST", 9],
            ["MATCH_STORE", (1, 1, 2, ("a", "b"))],
            ["MATCH_LOAD", ("skip_a", 0, 0, 0)],
            ["CONST", "loaded a"],
            ["RETURN", 1],
            ["LABEL", "skip_a"],
            ["MATCH_LOAD", ("skip_b", 0, 1, 0)],
        ] + hit(4) + miss(3, "skip_b")
        got = await run(prog, "v")
        self.assertEqual((got[0], got[3], got[4]), ("base", 9, "hit"))


    async def test_same(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["MATCH_SCOPE", (0, 2, 0, 0)],
            ["MATCH_NEXT", ("miss", 1, 0, 0)],
            ["MATCH_STORE", (1, 0, 2, ("a", "b"))],
            ["MATCH_NEXT", ("miss", 1, 0, 0)],
            ["MATCH_SAME", ("miss", 1, 0, 0)],
        ] + hit(3) + miss(3)
        self.assertEqual((await run(prog, (1, 1)))[3], "hit")
        self.assertEqual((await run(prog, (1, 2)))[3], "miss")
        self.assertEqual((await run(prog, cons(1, 1, nil)))[3], "hit")
        self.assertEqual((await run(prog, cons(1, 2, nil)))[3], "miss")

        # an empty slot misses, the element consumed
        prog[5] = ["MATCH_SAME", ("miss", 1, 1, 0)]
        got = await run(prog, (1, 1))
        self.assertEqual((got[0], got[3]), ("base", "miss"))


class TestItems(CevalTest):

    async def test_items_append_min(self):
        prog = [
            ["MATCH_ITEMS", 0],
            ["CONST", 1],
            ["MATCH_APPEND", (1, 0, 0, 0)],
            ["CONST", 2],
            ["MATCH_APPEND", (1, 0, 0, 0)],
            ["MATCH_MIN", ("miss", 0, 2, 0)],
            ["MATCH_MIN", ("miss", 0, 3, 0)],
        ] + hit(3) + miss(3)
        got = await run(prog, "v")
        self.assertEqual(got, ("base", "v", [1, 2], "miss"))


    async def test_columns(self):
        names = ("x", "y")
        prog = [
            ["MATCH_SINGLE", 0],
            ["MATCH_SCOPE", (0, 3, 0, 0)],
            ["MATCH_ITEMS", 0],
            ["VAR", "first"],
            ["MATCH_APPEND", (1, 0, 0, 0)],
            ["VAR", "second"],
            ["MATCH_APPEND", (1, 0, 0, 0)],
            ["MATCH_COLUMNS", (0, 1, names, (2, 0))],
            ["POP", 1],
            ["MATCH_CAPTURES", (0, 1, ("a", "b", "c"), 0)],
        ] + hit(4)
        values = {"first": {"x": 1, "y": 2}, "second": {"x": 3}}
        got = await run(prog, "v", values)
        self.assertEqual(got[3], {"c": [1, 3], "a": [2]})


class TestCopyOnShare(CevalTest):

    async def test_store_into_a_held_mark_copies_it(self):
        prog = [
            ["MATCH_SINGLE", 0],
            ["MATCH_SCOPE", (0, 1, 0, 0)],
            ["DUP", 1],
            ["CONST", 7],
            ["MATCH_STORE", (1, 0, 3, ("a",))],
            ["MATCH_CAPTURES", (0, 2, ("a",), 0)],
            ["MATCH_CAPTURES", (2, 3, ("a",), 0)],
        ] + hit(6)
        got = await run(prog, "v")
        self.assertEqual(got[4], {"a": 7})
        self.assertEqual(got[5], {})


    async def test_clear_and_columns_on_a_held_mark_copy_it(self):
        names = ("a", "b")
        prog = [
            ["MATCH_SINGLE", 0],
            ["MATCH_SCOPE", (0, 2, 0, 0)],
            ["CONST", 7],
            ["MATCH_STORE", (1, 0, 2, names)],
            ["DUP", 1],
            ["MATCH_CLEAR", (0, (0,), 0, 0)],
            ["DUP", 1],
            ["MATCH_ITEMS", 0],
            ["MATCH_COLUMNS", (0, 1, ("x",), (1,))],
            ["POP", 1],
            # base, cursor, the mark, its cleared copy, the copy's copy
            # with the column
            ["MATCH_CAPTURES", (0, 3, names, 0)],
            ["MATCH_CAPTURES", (2, 4, names, 0)],
            ["MATCH_CAPTURES", (4, 5, names, 0)],
        ] + hit(8)
        got = await run(prog, "v")
        self.assertEqual(got[5:], ({"b": []}, {}, {"a": 7}, "hit"))


    async def test_moving_a_held_cursor_copies_it(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["DUP", 1],
            ["MATCH_NEXT", ("miss", 0, 0, 0)],
            ["MATCH_NEXT", ("miss", 2, 0, 0)],
        ] + hit(5) + miss(0)
        got = await run(prog, (1, 2))
        self.assertEqual(got[3:], (1, 1, "hit"))
        got = await run(prog, cons(1, 2, nil))
        self.assertEqual((got[3].datum, got[4].datum), (1, 1))


    async def test_sync_on_a_held_cursor_copies_it(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["DUP", 1],
            ["MATCH_BOUND", ("miss", 0, 0, 0)],
            ["MATCH_NEXT", ("miss", 0, 0, 0)],
            ["POP", 1],
            ["MATCH_SYNC", (1, 0, 0, 0)],
            ["MATCH_FITS", ("miss", 1, 1, 1)],
            ["MATCH_FITS", ("miss", 2, 2, 2)],
        ] + hit(4) + miss(4)
        for subject in ((1, 2), cons(1, 2, nil)):
            self.assertEqual((await run(prog, subject))[4], "hit")


    async def test_tail_on_a_held_cursor_copies_it(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["DUP", 1],
            ["MATCH_TAIL", ("miss", 0, 0, 0)],
            ["MATCH_FITS", ("miss", 1, 0, 0)],
            ["MATCH_FITS", ("miss", 2, 2, 2)],
        ] + hit(4) + miss(4)
        for subject in ((1, 2), cons(1, 2, nil)):
            self.assertEqual((await run(prog, subject))[4], "hit")


    async def test_reset_on_a_held_cursor_copies_it(self):
        prog = [
            ["MATCH_DESCEND", "miss"],
            ["MATCH_SCOPE", (0, 0, 0, 0)],
            ["MATCH_NEXT", ("miss", 1, 0, 0)],
            ["POP", 1],
            ["DUP", 2],
            ["MATCH_RESET", (1, 0, 0, 0)],
            ["MATCH_FITS", ("miss", 1, 2, 2)],
            ["MATCH_FITS", ("miss", 3, 1, 1)],
        ] + hit(5) + miss(5)
        for subject in ((1, 2), cons(1, 2, nil)):
            self.assertEqual((await run(prog, subject))[5], "hit")


    async def test_append_to_a_held_list_copies_it(self):
        prog = [
            ["MATCH_ITEMS", 0],
            ["DUP", 1],
            ["CONST", 1],
            ["MATCH_APPEND", (1, 0, 0, 0)],
        ] + hit(4)
        got = await run(prog, "v")
        self.assertEqual(got[2:4], ([], [1]))


    async def test_append_to_a_list_held_twice_copies_it_once_per_writer(self):
        prog = [
            ["MATCH_ITEMS", 0],
            ["DUP", 1],
            ["DUP", 1],
            # the top entry takes its own copy
            ["CONST", 1],
            ["MATCH_APPEND", (1, 0, 0, 0)],
            # the middle entry, still sharing with the bottom, takes
            # another
            ["CONST", 2],
            ["MATCH_APPEND", (2, 0, 0, 0)],
            # the top entry's copy is its own; it grows in place
            ["CONST", 3],
            ["MATCH_APPEND", (1, 0, 0, 0)],
        ] + hit(5)
        got = await run(prog, "v")
        self.assertEqual(got[2:5], ([], [2], [1, 3]))


class TestElementTests(CevalTest):

    async def test_literal(self):
        prog = [
            ["MATCH_LITERAL", ("miss", 7, 0, 0)],
        ] + hit(1) + miss(1)
        self.assertEqual(await run(prog, 7), ("base", "hit"))
        self.assertEqual(await run(prog, Datum(7)), ("base", "hit"))
        self.assertEqual(await run(prog, 8), ("base", "miss"))


    async def test_core(self):
        prog = [
            ["MATCH_CORE", ("miss", int(CoreType.TUPLE), 0, 0)],
        ] + hit(1) + miss(1)
        self.assertEqual(await run(prog, (1,)), ("base", "hit"))
        self.assertEqual(await run(prog, [1]), ("base", "miss"))
        prog[0] = ["MATCH_CORE", ("miss", len(CoreType), 0, 0)]
        with self.assertRaises(SystemError):
            await run(prog, 1)


    async def test_type_twins(self):
        prog = [
            ["MATCH_TYPE_VAR", ("miss", "int", 0, 0)],
        ] + hit(1) + miss(1)
        values = {"int": int}
        self.assertEqual(await run(prog, 7, values), ("base", "hit"))
        self.assertEqual(await run(prog, "7", values), ("base", "miss"))

        prog[0] = ["MATCH_TYPE_FAST", ("miss", 1, 0, 0)]
        syms = ("subject", "int")
        self.assertEqual(await run(prog, 7, values, syms), ("base", "hit"))
        self.assertEqual(
            await run(prog, "7", values, syms), ("base", "miss"))


    async def test_class_twins(self):
        prog = [
            ["MATCH_CLASS_VAR", ("miss", "Point", 2, 0)],
        ] + hit(1) + miss(1)
        values = {"Point": Point}
        self.assertEqual(
            await run(prog, Point(3, 4), values), ("base", "hit"))
        self.assertEqual(
            await run(prog, Datum(Point(3, 4)), values), ("base", "hit"))
        self.assertEqual(await run(prog, 7, values), ("base", "miss"))

        prog[0] = ["MATCH_CLASS_FAST", ("miss", 1, 2, 0)]
        syms = ("subject", "Point")
        self.assertEqual(
            await run(prog, Point(3, 4), values, syms), ("base", "hit"))
        self.assertEqual(await run(prog, 7, values, syms), ("base", "miss"))


    async def test_class_count_past_match_args(self):
        # a non-instance misses; an instance raises
        for row in (["MATCH_CLASS_VAR", ("miss", "Point", 3, 0)],
                    ["MATCH_CLASS_FAST", ("miss", 1, 3, 0)]):
            prog = [row] + hit(1) + miss(1)
            values = {"Point": Point}
            syms = ("subject", "Point")
            self.assertEqual(
                await run(prog, 7, values, syms), ("base", "miss"))
            with self.assertRaises(TypeError) as caught:
                await run(prog, Point(3, 4), values, syms)
            self.assertIn(
                "__match_args__ has 2 entries; cannot satisfy 3 positional "
                "field pattern(s)", str(caught.exception))


class TestProjections(CevalTest):

    async def test_field_pos_twins(self):
        prog = [
            ["MATCH_FIELD_POS_VAR", ("miss", "Point", 1, 0)],
        ] + hit(3) + miss(2)
        values = {"Point": Point}
        got = await run(prog, Point(3, 4), values)
        self.assertEqual((got[0], got[2], got[3]), ("base", 4, "hit"))

        prog[0] = ["MATCH_FIELD_POS_FAST", ("miss", 1, 0, 0)]
        syms = ("subject", "Point")
        got = await run(prog, Point(3, 4), values, syms)
        self.assertEqual((got[2], got[3]), (3, "hit"))

        # a missing attribute misses with the object left
        p = Point(3, 4)
        del p.x
        got = await run(prog, p, values, syms)
        self.assertEqual((got[0], got[1], got[2]), ("base", p, "miss"))


    async def test_field_pos_past_match_args_raises(self):
        prog = [
            ["MATCH_FIELD_POS_VAR", ("miss", "Point", 2, 0)],
        ] + hit(3) + miss(2)
        with self.assertRaises(TypeError):
            await run(prog, Point(3, 4), {"Point": Point})


    async def test_field_kw(self):
        prog = [
            ["MATCH_FIELD_KW", ("miss", "y", 0, 0)],
        ] + hit(3) + miss(2)
        got = await run(prog, Point(3, 4))
        self.assertEqual((got[0], got[2], got[3]), ("base", 4, "hit"))
        prog[0] = ["MATCH_FIELD_KW", ("miss", "z", 0, 0)]
        got = await run(prog, Point(3, 4))
        self.assertEqual((got[0], got[2]), ("base", "miss"))


    async def test_mapping_and_keys(self):
        syms = ("subject", "key")
        values = {"key": "b"}
        subject = {"a": 1, "b": 2}
        for key, want in ((["MATCH_KEY", ("miss", "a", 0, 0)], 1),
                          (["MATCH_KEY_VAR", ("miss", "key", 0, 0)], 2),
                          (["MATCH_KEY_FAST", ("miss", 1, 0, 0)], 2)):
            prog = [
                ["MATCH_MAPPING", "miss"],
                key,
            ] + hit(3) + miss(2)
            got = await run(prog, subject, values, syms)
            self.assertEqual(got, ("base", subject, want, "hit"))

        prog = [
            ["MATCH_MAPPING", "miss"],
            ["MATCH_KEY", ("miss", "c", 0, 0)],
        ] + hit(3) + miss(2)
        got = await run(prog, {"a": 1})
        self.assertEqual((got[0], got[2]), ("base", "miss"))
        got = await run(prog, [1])
        self.assertEqual((got[0], got[2]), ("base", "miss"))


# The end.
