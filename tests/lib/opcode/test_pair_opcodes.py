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
Tests for PROPER, PAIR_SPLICE, CAR, and CDR

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import pytest

from koskari.lib import bracket_nil, bracket_pair, cons, nil, symbol
from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


class TestCarOpcode(CevalTest):
    """
    Tests for the CAR opcode: COUNT argument and chained car.
    """

    async def test_car_one(self):
        """
        CAR 1 returns the car of a cons cell.
        """

        cell = cons(10, 20, nil)
        code, ns = build_expr([
            ["CONST", cell],
            ["CAR", 1],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 10)


    async def test_car_two_chained(self):
        """
        CAR 2 applies car twice in the handler (one pop, local walk).
        """

        inner = cons(1, nil)
        cell = cons(inner, 2, nil)
        code, ns = build_expr([
            ["CONST", cell],
            ["CAR", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 1)


    async def test_car_invalid_count_zero_raises(self):
        """
        CAR 0 is rejected at runtime (invalid argument count).
        """

        code, ns = build_expr([
            ["CONST", cons(1, nil)],
            ["CAR", 0],
            ["RETURN", 1],
        ])
        with self.assertRaises(RuntimeError) as cm:
            await make_callable(code, ns)()
        self.assertIn("CAR invoked with invalid", str(cm.exception))
        self.assertIn("0", str(cm.exception))


    async def test_car_type_error_reports_index_and_type(self):
        """
        CAR on a non-pair reports index 0 and the Python type name.
        """

        code, ns = build_expr([
            ["CONST", "not-a-pair"],
            ["CAR", 1],
            ["RETURN", 1],
        ])
        with self.assertRaises(TypeError) as cm:
            await make_callable(code, ns)()
        msg = str(cm.exception)
        self.assertIn("CAR at index 0 of 1", msg)
        self.assertIn("str", msg)


    async def test_car_on_nil_reports_nil(self):
        """
        CAR on nil reports got nil at index 0.
        """

        code, ns = build_expr([
            ["CONST", nil],
            ["CAR", 1],
            ["RETURN", 1],
        ])
        with self.assertRaises(TypeError) as cm:
            await make_callable(code, ns)()
        msg = str(cm.exception)
        self.assertIn("CAR at index 0 of 1", msg)
        self.assertIn("nil", msg)


    async def test_car_second_step_type_error_reports_index_one(self):
        """
        CAR 2 fails on the second application with index 1 and the bad type.
        """

        inner = cons(99, nil)
        cell = cons(1, inner)
        code, ns = build_expr([
            ["CONST", cell],
            ["CAR", 2],
            ["RETURN", 1],
        ])
        with self.assertRaises(TypeError) as cm:
            await make_callable(code, ns)()
        msg = str(cm.exception)
        self.assertIn("CAR at index 1 of 2", msg)
        self.assertIn("int", msg)


class TestCdrOpcode(CevalTest):
    """
    Tests for the CDR opcode: COUNT argument and chained cdr.
    """

    async def test_cdr_one(self):
        """
        CDR 1 returns the cdr of a cons cell.
        """

        tail = cons(2, nil)
        cell = cons(1, tail)
        code, ns = build_expr([
            ["CONST", cell],
            ["CDR", 1],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, tail)


    async def test_cdr_two_chained(self):
        """
        CDR 2 walks two cons cells.
        """

        cell = cons(1, 2, 3, nil)
        code, ns = build_expr([
            ["CONST", cell],
            ["CDR", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual([3], list(result.unpack()))


    async def test_cdr_invalid_count_zero_raises(self):
        """
        CDR 0 is rejected at runtime.
        """

        code, ns = build_expr([
            ["CONST", cons(1, nil)],
            ["CDR", 0],
            ["RETURN", 1],
        ])
        with self.assertRaises(RuntimeError) as cm:
            await make_callable(code, ns)()
        self.assertIn("CDR invoked with invalid", str(cm.exception))


    async def test_cdr_type_error_reports_index_and_type(self):
        """
        CDR on a non-pair reports index 0 and the type.
        """

        code, ns = build_expr([
            ["CONST", 3.14],
            ["CDR", 1],
            ["RETURN", 1],
        ])
        with self.assertRaises(TypeError) as cm:
            await make_callable(code, ns)()
        msg = str(cm.exception)
        self.assertIn("CDR at index 0 of 1", msg)
        self.assertIn("float", msg)


    async def test_cdr_second_step_reports_nil(self):
        """
        CDR on a pair whose cdr is nil fails at index 1 with got nil.
        """

        cell = cons(1, nil)
        code, ns = build_expr([
            ["CONST", cell],
            ["CDR", 2],
            ["RETURN", 1],
        ])
        with self.assertRaises(TypeError) as cm:
            await make_callable(code, ns)()
        msg = str(cm.exception)
        self.assertIn("CDR at index 1 of 2", msg)
        self.assertIn("nil", msg)


class TestProperOpcode(CevalTest):
    """
    Tests for the PROPER opcode: build a proper list from N stack elements
    """

    async def test_proper_zero_pushes_nil(self):
        """
        PROPER 0 pushes nil (the empty proper list)
        """

        code, ns = build_expr([
            ["PROPER", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, nil)


    async def test_proper_one_element(self):
        """
        PROPER 1 wraps a single element as (elem . nil)
        """

        code, ns = build_expr([
            ["CONST", 42],
            ["PROPER", 1],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual([42], list(result.unpack()))


    async def test_proper_three_elements(self):
        """
        PROPER 3 builds (1 . (2 . (3 . nil)))
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["CONST", 3],
            ["PROPER", 3],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual([1, 2, 3], list(result.unpack()))


    async def test_proper_with_symbols(self):
        """
        PROPER works with symbol constants — the common quasi-quote case
        """

        a = symbol("a")
        b = symbol("b")
        code, ns = build_expr([
            ["CONST", a],
            ["CONST", b],
            ["PROPER", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual([a, b], list(result.unpack()))


    async def test_proper_result_is_proper(self):
        """
        PROPER always produces a list whose is_proper() is True
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["PROPER", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertTrue(result.is_proper())


class TestBracketConsOpcode(CevalTest):
    """
    BRACKET_CONS: the twin of CONS over bracket pairs
    """

    async def test_bracket_cons_zero_pushes_bracket_nil(self):
        code, ns = build_expr([
            ["BRACKET_CONS", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, bracket_nil)

    async def test_bracket_cons_builds_a_bracket_pair_with_a_tail(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["BRACKET_CONS", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIsInstance(result, bracket_pair)
        self.assertEqual((result.car, result.cdr), (1, 2))

    async def test_bracket_cons_with_bracket_nil_tail_is_proper(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["CONST", bracket_nil],
            ["BRACKET_CONS", 3],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIsInstance(result, bracket_pair)
        self.assertEqual([1, 2], list(result.unpack()))
        self.assertTrue(result.is_proper())


class TestBracketProperOpcode(CevalTest):
    """
    BRACKET_PROPER: the twin of PROPER over bracket pairs
    """

    async def test_bracket_proper_zero_pushes_bracket_nil(self):
        code, ns = build_expr([
            ["BRACKET_PROPER", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, bracket_nil)

    async def test_bracket_proper_three_elements(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["CONST", 3],
            ["BRACKET_PROPER", 3],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIsInstance(result, bracket_pair)
        self.assertEqual([1, 2, 3], list(result.unpack()))
        self.assertIs(result.cdr.cdr.cdr, bracket_nil)


class TestPairSpliceOpcode(CevalTest):
    """
    Tests for the PAIR_SPLICE opcode: concatenate N pair/tuple segments
    """

    async def test_pair_splice_zero_pushes_nil(self):
        """
        PAIR_SPLICE 0 pushes nil
        """

        code, ns = build_expr([
            ["PAIR_SPLICE", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, nil)


    async def test_pair_splice_single_proper_pair(self):
        """
        PAIR_SPLICE 1 with a single proper list returns that list
        """

        from koskari.lib import cons
        seg = cons(1, 2, nil)
        code, ns = build_expr([
            ["CONST", seg],
            ["PAIR_SPLICE", 1],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual([1, 2], list(result.unpack()))


    async def test_pair_splice_two_pairs(self):
        """
        PAIR_SPLICE 2 concatenates two proper lists
        """

        from koskari.lib import cons
        seg1 = cons(1, 2, nil)
        seg2 = cons(3, 4, nil)
        code, ns = build_expr([
            ["CONST", seg1],
            ["CONST", seg2],
            ["PAIR_SPLICE", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual([1, 2, 3, 4], list(result.unpack()))


    async def test_pair_splice_tuple_segment(self):
        """
        PAIR_SPLICE accepts a tuple as a segment (the const-opt fast path)
        """

        a = symbol("a")
        b = symbol("b")
        code, ns = build_expr([
            ["CONST", (a, b)],
            ["PAIR_SPLICE", 1],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual([a, b], list(result.unpack()))


    async def test_pair_splice_tuple_and_pair(self):
        """
        PAIR_SPLICE handles a mix of a tuple segment and a pair segment
        """

        from koskari.lib import cons
        a = symbol("a")
        b = symbol("b")
        c = symbol("c")
        seg_pair = cons(c, nil)
        code, ns = build_expr([
            ["CONST", (a, b)],
            ["CONST", seg_pair],
            ["PAIR_SPLICE", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual([a, b, c], list(result.unpack()))


    async def test_pair_splice_nil_segment_skipped(self):
        """
        PAIR_SPLICE skips nil segments gracefully
        """

        from koskari.lib import cons
        seg = cons(1, 2, nil)
        code, ns = build_expr([
            ["CONST", nil],
            ["CONST", seg],
            ["PAIR_SPLICE", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual([1, 2], list(result.unpack()))


    async def test_pair_splice_result_is_proper(self):
        """
        PAIR_SPLICE always produces a proper list
        """

        from koskari.lib import cons
        seg1 = cons(1, nil)
        seg2 = cons(2, nil)
        code, ns = build_expr([
            ["CONST", seg1],
            ["CONST", seg2],
            ["PAIR_SPLICE", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertTrue(result.is_proper())


    async def test_pair_splice_three_segments(self):
        """
        PAIR_SPLICE 3 concatenates three segments in order
        """

        a = symbol("a")
        b = symbol("b")
        c = symbol("c")
        code, ns = build_expr([
            ["CONST", (a,)],
            ["CONST", (b,)],
            ["CONST", (c,)],
            ["PAIR_SPLICE", 3],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual([a, b, c], list(result.unpack()))


    async def test_pair_splice_empty_tuple_segments(self):
        """
        PAIR_SPLICE with empty tuple segments produces nil
        """

        code, ns = build_expr([
            ["CONST", ()],
            ["CONST", ()],
            ["PAIR_SPLICE", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, nil)


# The end.
