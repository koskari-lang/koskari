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
tests.reproducers.test_foreach_pyaiter_anext_error

Defect reproducer, RED until fixed: koskari for-each over a python
async iterator whose __anext__ fails before producing an awaitable
must report that failure. get_await_next returns NULL both when the
object has no am_anext and when am_anext or GetAwaitable raised, and
the iteration dispatcher read every NULL as the former, so the real
exception was dropped for a TypeError naming the object as not an
iterator. A synchronous StopAsyncIteration from the same call is the
iterator's exhaustion, as it is once the awaitable is stepped.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import asyncio
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def collect [ait]:
    var out (#list)
    for-each [x ait]:
        (out.append x)
    out

define collect_caught [ait]:
    define out (#list)
    try:
        for-each [x ait]:
            (out.append x)
    except [e RuntimeError]:
        (out.append (str e))
    for-each [y #[7 8]]:
        (out.append y)
    out
"""


class RaisingAnext:
    """
    An async iterator whose __anext__ raises before it can hand back
    an awaitable, as a running or closed async generator's does
    """

    def __aiter__(self):
        return self

    def __anext__(self):
        raise RuntimeError("anext refused")


class StopAnext:
    """
    An async iterator that signals exhaustion from __anext__ itself
    rather than from the awaitable it would have returned
    """

    def __aiter__(self):
        return self

    def __anext__(self):
        raise StopAsyncIteration


class RaisingAfterAwait:
    """
    An async iterator whose step awaits and then fails
    """

    def __init__(self, good):
        self.good = good

    def __aiter__(self):
        return self

    async def __anext__(self):
        await asyncio.sleep(0)
        if self.good:
            self.good -= 1
            return self.good
        raise RuntimeError("anext failed after await")


class TestForeachPyAiterAnextError(unittest.IsolatedAsyncioTestCase):
    """
    for-each over a python async iterator reports what __anext__ did
    """

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module

    async def test_anext_error_reaches_the_caller(self):
        """
        a RuntimeError out of __anext__ propagates unchanged
        """

        mod = await self.load("fpa_anext_raise")
        collect = vars(mod)["collect"]

        with self.assertRaises(RuntimeError) as caught:
            await collect(RaisingAnext())
        self.assertEqual(str(caught.exception), "anext refused")

    async def test_anext_stop_async_is_exhaustion(self):
        """
        StopAsyncIteration out of __anext__ ends the loop empty
        """

        mod = await self.load("fpa_anext_stop")
        collect = vars(mod)["collect"]

        self.assertEqual(await collect(StopAnext()), [])

    async def test_anext_error_after_await_reaches_the_try(self):
        """
        a RuntimeError raised after the step awaited reaches the
        enclosing try, and a later for-each in the same frame runs
        """

        mod = await self.load("fpa_anext_await_raise")
        self.assertEqual(
            await vars(mod)["collect_caught"](RaisingAfterAwait(2)),
            [1, 0, "anext failed after await", 7, 8])


# The end.
