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
Tests for NOOP, LABEL, POSITION opcodes (no-ops at runtime).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import pytest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


class TestNoopLabelPositionOpcodes(CevalTest):
    """
    Tests for NOOP, LABEL, POSITION (no-ops / compile-time only at runtime).
    """

    async def test_noop_advances_pc(self):
        """
        NOOP is a no-op; execution continues to next instruction
        """

        code, ns = build_expr([
            ["NOOP", 0],
            ["CONST", 1],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 1)


    async def test_label_is_noop_at_runtime(self):
        """
        LABEL is compile-time only; at runtime it advances PC
        """

        code, ns = build_expr([
            ["LABEL", "here"],
            ["CONST", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 2)


    async def test_position_is_noop_at_runtime(self):
        """
        POSITION is compile-time only; at runtime it advances PC
        """

        code, ns = build_expr([
            ["POSITION", (0, 0)],
            ["CONST", 3],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 3)


# The end.
