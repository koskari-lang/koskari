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
tests.lib.opcode.test_foreach_async_slot_lost

A for-each bound to a Python iterator with both __anext__ and __next__
takes the async step. When the class loses __anext__ during the loop,
the remaining steps take __next__; when it has neither, the loop raises
TypeError rather than failing without an exception.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define [collect it side]
  (define out #[])
  (for-each [x it]
    (side x)
    (out.append x))
  out)
"""


def make_cls():
    class Cls:
        def __init__(self, n):
            self.i = 0
            self.n = n

        def __aiter__(self):
            return self

        def __iter__(self):
            return self

        async def __anext__(self):
            if self.i >= self.n:
                raise StopAsyncIteration
            self.i += 1
            return ("async", self.i)

        def __next__(self):
            if self.i >= self.n:
                raise StopIteration
            self.i += 1
            return ("sync", self.i)


    return Cls


class TestForeachAsyncSlotLost(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("foreach_async_slot_lost")
        vars(module)["__file__"] = "<foreach_async_slot_lost>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_losing_anext_falls_back_to_next(self):
        mod = await self.load()

        Cls = make_cls()

        def side(x):
            if x == ("async", 2):
                del Cls.__anext__

        out = await mod["collect"](Cls(4), side)
        self.assertEqual(out, [("async", 1), ("async", 2), ("sync", 3),
                               ("sync", 4)])

    async def test_losing_both_raises_type_error(self):
        mod = await self.load()

        Cls = make_cls()

        def side(x):
            if x == ("async", 1):
                del Cls.__anext__
                del Cls.__next__

        with self.assertRaises(TypeError):
            await mod["collect"](Cls(4), side)


# The end.
