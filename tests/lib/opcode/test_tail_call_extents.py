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
tests.lib.opcode.test_tail_call_extents

Tail-call frame reuse against open extent nodes: discardable extents
(NS, LOOP, FOREACH) uninstall at the handoff, and any other open node
kind refuses the tail call loudly instead of discarding an extent
whose uninstall behavior must run

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestTailCallExtents(CevalTest):
    """
    Tail calls interacting with dynamic extent nodes
    """

    async def test_tail_call_through_foreach_extent_drops_and_calls(self):
        """
        A tail call under an open FOREACH extent drops the node at the
        handoff and completes with the callee's value
        """

        inner_code, inner_ns = build_expr([
            ["CONST", 42],
            ["RETURN", 1],
        ], name="inner")
        inner = make_callable(inner_code, inner_ns)

        code, ns = build_expr([
            ["CONST", (1, 2)],
            ["PUSH_FOREACH", 0],
            ["VAR", "fn"],
            ["TAIL_CALL", 0],
            ["POP_FOREACH", 0],
            ["RETURN", 1],
        ], values={"fn": inner})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_tail_call_through_cc_extent_refuses(self):
        """
        A tail call under an open CC extent raises rather than
        discarding a node whose uninstall must run
        """

        inner_code, inner_ns = build_expr([
            ["CONST", 42],
            ["RETURN", 1],
        ], name="inner")
        inner = make_callable(inner_code, inner_ns)

        code, ns = build_expr([
            ["PUSH_CC", 0],
            ["VAR", "fn"],
            ["TAIL_CALL", 0],
            ["POP_CC", 0],
            ["RETURN", 1],
        ], values={"fn": inner})
        with self.assertRaises(RuntimeError) as ctx:
            await make_callable(code, ns)()
        self.assertIn("non-discardable", str(ctx.exception))


    async def test_yield_with_tail_twin_rejected_at_construction(self):
        """
        The C validator refuses a stream mixing YIELD with any TAIL
        family opcode, twins included; direct Code construction cannot
        bypass the compiler-side demotion
        """

        with self.assertRaises(ValueError) as ctx:
            build_expr([
                ["YIELD", 1],
                ["VAR", "f"],
                ["TAIL_CALL", 0],
                ["RETURN", 1],
            ])
        self.assertIn("TAIL_CALL", str(ctx.exception))

        with self.assertRaises(ValueError) as ctx:
            build_expr([
                ["YIELD", 1],
                ["TAIL_CALL_VAR", ("f", 0, 0, 0)],
                ["RETURN", 1],
            ])
        self.assertIn("TAIL_CALL", str(ctx.exception))


# The end.
