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
Tests for DUP and POP opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestStackOpcodes(CevalTest):
    """
    Tests for DUP, POP
    """

    async def test_dup_copies_stack_top(self):
        """
        DUP n duplicates top n values; RETURN 2 returns (a, a)
        """

        code, ns = build_expr([
            ["CONST", 7],
            ["DUP", 1],
            ["RETURN", 2],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, (7, 7))


    async def test_pop_discards_stack_values(self):
        """
        POP n discards top n values; remaining value is returned
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 20],
            ["POP", 1],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 10)


# The end.
