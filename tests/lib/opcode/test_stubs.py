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
Tests for opcodes with no runtime implementation.

THEEND is an end-of-code sentinel; execution reaching it raises a fixed
RuntimeError. This module documents that expected behavior.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestStubOpcodes(CevalTest):
    """
    Tests for opcodes that have no runtime implementation.
    Each test runs code that hits such an opcode and expects RuntimeError.
    """

    async def _run_stub_opcode(self, opname, arg=0):
        """
        Build and run bytecode that hits the given stub opcode
        """

        code_obj, ns = build_expr([
            [opname, arg],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        return await expr()


    async def test_theend_raises_unimpl(self):
        """
        THEEND has no runtime implementation; raises RuntimeError with fixed message
        """

        with self.assertRaises(RuntimeError) as ctx:
            await self._run_stub_opcode("THEEND", 0)
        self.assertIn("THEEND", str(ctx.exception))
        self.assertIn("no runtime implementation", str(ctx.exception))


# The end.
