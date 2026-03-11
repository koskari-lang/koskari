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
Tests for YIELD and async gen opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr
from koskari.lib import AsyncGenerator

import pytest

pytestmark = pytest.mark.phase_lib


try:
    anext
except NameError:
    # anext wasn't available until Python 3.10
    async def anext(agen):
        return await agen.__anext__()


class TestYieldOpcodes(CevalTest):
    """
    Tests for YIELD opcode and async generators.
    """

    async def test_yielding_expression_returns_async_generator(self):
        """
        Calling an Expression whose code has YIELD returns AsyncGenerator
        """
        gen_code, gen_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(gen_code, gen_ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)


    async def test_yield_suspends_and_resumes(self):
        """
        Yielding code returns AsyncGenerator; anext() yields value then StopAsyncIteration
        """

        code = [
            ["CONST", 42],
            ["YIELD", 1],
            ["RETURN", 0],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            v = await anext(agen)
            self.assertEqual(v, 42)
            with self.assertRaises(StopAsyncIteration):
                await anext(agen)

        await run()


# The end.
