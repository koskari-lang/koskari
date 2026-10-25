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
tests.lib.test_coroutine_first_send

The first send into an unstarted Koskari coroutine must carry None, as
for a CPython coroutine: a non-None value raises TypeError and leaves
the coroutine unstarted, so a following send of None runs it. A send
that completes with a return value chains the handled exception onto
the StopIteration's __context__ only where a CPython coroutine does on
the running version.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

import koskari.lib as lib
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define box (list))

(define add (lambda [a b] (box.append "ran") (+ a b)))
"""


class TestCoroutineFirstSend(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("coroutine_first_send")
        vars(module)["__file__"] = "<coroutine_first_send>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_non_none_first_send_raises_and_leaves_it_unstarted(self):
        mod = await self.load()
        coro = mod["add"](1, 2)
        with self.assertRaises(TypeError):
            coro.send(5)
        self.assertEqual(mod["box"], [])
        self.assertIn("new", repr(coro))
        with self.assertRaises(StopIteration) as caught:
            coro.send(None)
        self.assertEqual(caught.exception.value, 3)
        self.assertEqual(mod["box"], ["ran"])

    async def test_first_send_of_repeated_calls_runs_each(self):
        mod = await self.load()
        add = mod["add"]
        for i in range(10):
            coro = add(i, 2)
            with self.assertRaises(StopIteration) as caught:
                coro.send(None)
            self.assertEqual(caught.exception.value, i + 2)
        self.assertEqual(mod["box"], ["ran"] * 10)

    async def test_send_return_chains_context_as_cpython_does(self):
        mod = await self.load()

        async def native_add(a, b):
            return a + b

        native = native_add(1, 2)
        koskari = mod["add"](1, 2)

        try:
            raise ValueError("handled")
        except ValueError:
            with self.assertRaises(StopIteration) as native_caught:
                native.send(None)
            with self.assertRaises(StopIteration) as koskari_caught:
                koskari.send(None)

        native_stop = native_caught.exception
        koskari_stop = koskari_caught.exception
        self.assertEqual(native_stop.value, 3)
        self.assertEqual(koskari_stop.value, 3)
        self.assertEqual(
            native_stop.__context__ is None,
            koskari_stop.__context__ is None)

    async def test_defer_send_return_chains_context_as_cpython_does(self):
        mod = await self.load()

        async def native_add(a, b):
            return a + b

        native = native_add(1, 2)
        koskari = lib.defer(mod["add"], 1, 2)

        try:
            raise ValueError("handled")
        except ValueError:
            with self.assertRaises(StopIteration) as native_caught:
                native.send(None)
            with self.assertRaises(StopIteration) as koskari_caught:
                koskari.send(None)

        native_stop = native_caught.exception
        koskari_stop = koskari_caught.exception
        self.assertEqual(native_stop.value, 3)
        self.assertEqual(koskari_stop.value, 3)
        self.assertEqual(
            native_stop.__context__ is None,
            koskari_stop.__context__ is None)


# The end.
