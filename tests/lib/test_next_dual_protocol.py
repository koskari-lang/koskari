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
tests.lib.test_next_dual_protocol

A Python object that answers both the sync and the async iterator
protocols: next takes its __next__, as Python's next does, while
for-each prefers its __anext__

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define [take_next it] (next it))

(define [collect it]
  (var out (list))
  (for-each [x it] (out.append x))
  out)
"""


class Dual:
    """
    __next__ counts up from one; __anext__ counts up from ten
    """

    def __init__(self):
        self.sync = iter([1, 2, 3])
        self.async_ = iter([10, 20, 30])

    def __iter__(self):
        return self

    def __next__(self):
        return next(self.sync)

    def __aiter__(self):
        return self

    async def __anext__(self):
        try:
            return next(self.async_)
        except StopIteration:
            raise StopAsyncIteration


class TestNextDualProtocol(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("next_dual_protocol")
        vars(module)["__file__"] = "<next_dual_protocol>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_next_takes_the_sync_protocol(self):
        mod = await self.load()
        dual = Dual()
        self.assertEqual(await mod["take_next"](dual), 1)
        self.assertEqual(await mod["take_next"](dual), 2)

    async def test_for_each_takes_the_async_protocol(self):
        mod = await self.load()
        self.assertEqual(await mod["collect"](Dual()), [10, 20, 30])


# The end.
