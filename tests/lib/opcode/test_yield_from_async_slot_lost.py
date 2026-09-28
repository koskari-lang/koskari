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
tests.lib.opcode.test_yield_from_async_slot_lost

A yield-from over a Python async iterator steps it through __anext__.
When the class loses __anext__ between steps, the next step raises
TypeError rather than failing without an exception.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import pytest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


def make_counter():
    class Counter:
        def __init__(self):
            self.i = 0

        def __aiter__(self):
            return self

        async def __anext__(self):
            self.i += 1
            return self.i

    return Counter


class TestYieldFromAsyncSlotLost(CevalTest):

    async def test_lost_anext_raises_type_error(self):
        Counter = make_counter()
        code_obj, ns = build_expr([
            ["CONST", Counter()],
            ["YIELD_FROM", 0],
            ["RETURN", 0],
        ])
        agen = make_callable(code_obj, ns)()

        self.assertEqual(await agen.__anext__(), 1)
        del Counter.__anext__
        with self.assertRaises(TypeError):
            await agen.__anext__()


# The end.
