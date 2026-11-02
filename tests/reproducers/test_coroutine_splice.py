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
tests.reproducers.test_coroutine_splice

Full-stack reproducer: a fresh Koskari coroutine handed to the await
path by a Python wrapper is installed on the running chain instead of
being driven on its own context.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import functools
import os
import unittest

import pytest

from koskari.lib import KoskariSyncViolation
from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'coroutine_splice.ki')


class TestCoroutineSplice(unittest.IsolatedAsyncioTestCase):
    """
    Koskari coroutines reaching the await path from Python wrappers
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.coroutine_splice')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        vars(mod)['sleeper_partial'] = functools.partial(mod.sleeper)
        return mod


    async def test_partial_runs_on_the_calling_chain(self):
        """
        Non-tail recursion through functools.partial meets the caller's
        frame-depth limit; a nested context per level would reset it
        """

        mod = await self.load_module()
        self.assertEqual(await mod.via_partial(200), 200)
        with self.assertRaises(RecursionError):
            await mod.via_partial(2000)


    async def test_defer_runs_on_the_calling_chain(self):
        mod = await self.load_module()
        self.assertEqual(await mod.via_defer(200), 200)
        with self.assertRaises(RecursionError):
            await mod.via_defer(2000)


    async def test_awaited_coroutine_object_is_consumed(self):
        """
        An explicit await of a fresh coroutine object delivers its value,
        and the object is spent for Python afterwards
        """

        mod = await self.load_module()
        coro = mod.add(1, 2)
        self.assertEqual(await mod.await_object(coro), 3)
        with self.assertRaises(RuntimeError):
            coro.send(None)


    async def test_started_coroutine_refuses_a_second_awaiter(self):
        """
        A coroutine Python already started owes its next reply to the
        party that received its yield; a Koskari await of it is a second
        awaiter and is refused, as CPython refuses one, rather than
        taken fresh onto the chain or driven in the first party's place
        """

        mod = await self.load_module()
        coro = mod.sleeper()
        coro.send(None)
        with self.assertRaises(RuntimeError) as caught:
            await mod.await_object(coro)
        self.assertEqual(
            str(caught.exception), "coroutine is being awaited already")
        coro.close()


    async def test_demand_denies_a_wrapped_suspension(self):
        mod = await self.load_module()
        with self.assertRaises(KoskariSyncViolation):
            await mod.demand_over_partial()


# The end.
