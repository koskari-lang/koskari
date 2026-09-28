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
tests.lib.test_await_iterable_coroutine

An explicit await takes a generator marked by types.coroutine as its
own await iterator, as CPython's await does: its yields reach the
driver and its return value is the await's result. A plain generator
is not awaitable and comes back as a value.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import types
import unittest

import pytest

from koskari import lib
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


@types.coroutine
def tick():
    got = yield "tick"
    return got * 2


def plain():
    yield "plain"


SRC = """
(define waiter (lambda [aw] (await aw)))
"""


class TestAwaitIterableCoroutine(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("await_iterable_coroutine")
        vars(module)["__file__"] = "<await_iterable_coroutine>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_marked_generator_is_awaited(self):
        mod = await self.load()
        coro = mod["waiter"](tick())
        self.assertEqual(coro.send(None), "tick")
        with self.assertRaises(StopIteration) as done:
            coro.send(21)
        self.assertEqual(done.exception.value, 42)

    async def test_marked_generator_is_awaited_through_a_defer(self):
        mod = await self.load()
        coro = mod["waiter"](lib.defer(tick))
        self.assertEqual(coro.send(None), "tick")
        with self.assertRaises(StopIteration) as done:
            coro.send(21)
        self.assertEqual(done.exception.value, 42)

    async def test_plain_generator_is_a_value(self):
        mod = await self.load()
        gen = plain()
        coro = mod["waiter"](gen)
        with self.assertRaises(StopIteration) as done:
            coro.send(None)
        self.assertIs(done.exception.value, gen)
        gen.close()


# The end.
