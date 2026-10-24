# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
tests.reproducers.test_pyaiter_plain_awaitable

Defect reproducer: a python async iterator whose __anext__ returns a
plain awaitable, an object whose __await__ hands back an iterator
object rather than itself. The for-each step, next and yield-from
resolved the __anext__ result to its await iterator and the eager step
resolved that iterator again, asking it for __await__, so each failed
with AttributeError. The awaitable is resolved once.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
define collect_each [it]:
    define out (#list)
    for-each [x it]:
        (out.append x)
    out

define take_next [it]:
    (next it)

define relay [it]:
    yield-from it

define collect_gen [g]:
    define out (#list)
    for-each [x g]:
        (out.append x)
    out
"""


class PlainAwaitIterator:
    """
    The await iterator of a PlainAwaitable: yields None once, then
    raises the given exception
    """

    def __init__(self, exc):
        self.exc = exc
        self.stepped = False

    def __iter__(self):
        return self

    def __next__(self):
        if not self.stepped:
            self.stepped = True
            return None
        raise self.exc


class PlainAwaitable:
    """
    An awaitable whose __await__ returns a separate iterator object,
    which has no __await__ of its own
    """

    def __init__(self, exc):
        self.exc = exc

    def __await__(self):
        return PlainAwaitIterator(self.exc)


class PlainAwaitableIterator:
    """
    An async iterator whose __anext__ returns a PlainAwaitable
    delivering each value, then one ending with StopAsyncIteration
    """

    def __init__(self, values):
        self.values = list(values)

    def __aiter__(self):
        return self

    def __anext__(self):
        if self.values:
            return PlainAwaitable(StopIteration(self.values.pop(0)))
        return PlainAwaitable(StopAsyncIteration())


async def answering():
    got = yield 0
    await asyncio.sleep(0)
    yield ("got", got)


class TestPyAiterPlainAwaitable(unittest.IsolatedAsyncioTestCase):
    """
    a python async iterator stepping through a plain awaitable
    """

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_foreach_collects(self):
        """
        for-each over the iterator collects each value
        """

        ns = await self.load("ppa_foreach")
        found = await ns["collect_each"](PlainAwaitableIterator([1, 2, 3]))
        self.assertEqual(found, [1, 2, 3])


    async def test_next_first_value(self):
        """
        next on the iterator returns its first value
        """

        ns = await self.load("ppa_next")
        found = await ns["take_next"](PlainAwaitableIterator([1, 2, 3]))
        self.assertEqual(found, 1)


    async def test_yield_from_relays(self):
        """
        yield-from over the iterator relays each value
        """

        ns = await self.load("ppa_relay")
        relayed = ns["relay"](PlainAwaitableIterator([1, 2, 3]))
        found = await ns["collect_gen"](relayed)
        self.assertEqual(found, [1, 2, 3])


    async def test_yield_from_asend(self):
        """
        a value sent through the relay reaches a python async
        generator delegate through its asend
        """

        ns = await self.load("ppa_asend")
        g = ns["relay"](answering())
        self.assertEqual(await g.__anext__(), 0)
        self.assertEqual(await g.asend("x"), ("got", "x"))
        await g.aclose()


# The end.
