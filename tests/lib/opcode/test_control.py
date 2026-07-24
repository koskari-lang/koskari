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
Tests for RETURN, AWAIT opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import sys

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestControlOpcodes(CevalTest):
    """
    Tests for RETURN, AWAIT.
    """

    async def test_return_multiple_values(self):
        """
        RETURN n returns n values as a tuple
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["CONST", 3],
            ["RETURN", 3],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, (1, 2, 3))


    async def test_await_suspends_and_resumes_with_value(self):
        """
        AWAIT suspends on awaitable; resumes with result
        """

        async def return_five():
            return 5
        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["AWAIT", 1],
            ["RETURN", 1],
        ], values={"f": return_five})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 5)


    async def test_send_completed_await_result_refcount(self):
        """
        an await completed by the ordinary send drive delivers its
        result without retaining stray references
        """

        marker = object()

        class YieldingAwaitable:
            def __await__(self):
                yield "token"
                return marker

        code, ns = build_expr([
            ["VAR", "aw"],
            ["AWAIT", 1],
            ["RETURN", 1],
        ], values={"aw": YieldingAwaitable()})

        base = sys.getrefcount(marker)

        coro = make_callable(code, ns)()
        self.assertEqual(coro.send(None), "token")
        with self.assertRaises(StopIteration) as cm:
            coro.send(None)
        self.assertIs(cm.exception.value, marker)
        del cm
        del coro

        self.assertEqual(sys.getrefcount(marker), base)


    async def test_throw_completed_await_result_refcount(self):
        """
        an await completed by throw (the awaitable converts the thrown
        exception into a StopIteration value) delivers its result
        without retaining stray references
        """

        marker = object()

        class ConvertingAwaitable:
            def __await__(self):
                try:
                    yield "token"
                except KeyError:
                    return marker

        code, ns = build_expr([
            ["VAR", "aw"],
            ["AWAIT", 1],
            ["RETURN", 1],
        ], values={"aw": ConvertingAwaitable()})

        base = sys.getrefcount(marker)

        coro = make_callable(code, ns)()
        self.assertEqual(coro.send(None), "token")
        with self.assertRaises(StopIteration) as cm:
            coro.throw(KeyError())
        self.assertIs(cm.exception.value, marker)
        del cm
        del coro

        self.assertEqual(sys.getrefcount(marker), base)


# The end.
