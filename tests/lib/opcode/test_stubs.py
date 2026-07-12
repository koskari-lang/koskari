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
Tests for the THEEND end-of-code barrier.

THEEND is not a user instruction: it is the barrier appended to every code
object during assembly, so a program counter that walks off the end faults at
it instead of overrunning the code array. This module documents that it cannot
be supplied as an instruction.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestTheEndBarrier(CevalTest):
    """
    THEEND is the reserved end-of-code barrier; users cannot emit it.
    """

    async def test_theend_rejected_as_input(self):
        """
        Supplying THEEND as an instruction is rejected at construction, since
        it is the barrier the assembler appends itself.
        """

        with self.assertRaises(RuntimeError) as ctx:
            build_expr([
                ["THEEND", 0],
                ["RETURN", 1],
            ])
        self.assertIn("THEEND", str(ctx.exception))


# The end.
