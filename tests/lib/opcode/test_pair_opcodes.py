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
Tests for PROPER and PAIR_SPLICE opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""

from koskari.lib import nil, symbol

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


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
        seg = cons(1, cons(2, nil))
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
        seg1 = cons(1, cons(2, nil))
        seg2 = cons(3, cons(4, nil))
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
        seg = cons(1, cons(2, nil))
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
