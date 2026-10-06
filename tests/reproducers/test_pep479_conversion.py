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
tests.reproducers.test_pep479_conversion

A StopIteration escaping a generator body, a StopAsyncIteration
escaping one, or a StopIteration escaping a coroutine body is not the
protocol's own end signal: it becomes a RuntimeError with the original
as its cause (PEP 479, PEP 525), on every surface -- a Python next or
list, a Python async for, an await from Python, and a Koskari for-each
over the same generator. A provider whose source runs out still ends
normally; that is exhaustion delivered, not an exception escaping a
body.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
define leakgen [it]:
    (yield 1)
    (yield (next it))
    (yield 3)

define aleakgen [ait]:
    (yield 1)
    (yield (await (ait.__anext__)))
    (yield 3)

define-coroutine leakcoro [it]:
    (next it)

define consume [gen]:
    define col #[]
    try:
        (for-each [v gen]
            (col.append v))
        #["completed" col]
    except [StopIteration]:
        #["stop" col]
    except [RuntimeError as: e]:
        #["runtime" col (str e) (type e.__cause__).__name__]

define aconsume [agen]:
    define col #[]
    try:
        (for-each [v agen]
            (col.append v))
        #["completed" col]
    except [StopAsyncIteration]:
        #["stop" col]
    except [RuntimeError as: e]:
        #["runtime" col (str e) (type e.__cause__).__name__]

define mapped [it]:
    (list (map next #[it it it]))
"""


class Empty:
    def __aiter__(self):
        return self

    async def __anext__(self):
        raise StopAsyncIteration


class TestPep479Conversion(unittest.IsolatedAsyncioTestCase):

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module


    async def test_python_list_over_generator(self):
        mod = await self.load("p479_list")
        gen = vars(mod)["leakgen"](iter([]))
        with self.assertRaises(RuntimeError) as caught:
            list(gen)
        self.assertEqual("generator raised StopIteration", str(caught.exception))
        self.assertIsInstance(caught.exception.__cause__, StopIteration)


    async def test_python_async_for_over_generator(self):
        mod = await self.load("p479_afor")
        agen = vars(mod)["leakgen"](iter([]))
        got = []
        with self.assertRaises(RuntimeError) as caught:
            async for v in agen:
                got.append(v)
        self.assertEqual([1], got)
        self.assertEqual("generator raised StopIteration", str(caught.exception))
        self.assertIsInstance(caught.exception.__cause__, StopIteration)


    async def test_python_async_for_stop_async_iteration(self):
        mod = await self.load("p525_afor")
        agen = vars(mod)["aleakgen"](Empty())
        got = []
        with self.assertRaises(RuntimeError) as caught:
            async for v in agen:
                got.append(v)
        self.assertEqual([1], got)
        self.assertEqual(
            "generator raised StopAsyncIteration", str(caught.exception))
        self.assertIsInstance(caught.exception.__cause__, StopAsyncIteration)


    async def test_await_coroutine_from_python(self):
        mod = await self.load("p479_coro")
        with self.assertRaises(RuntimeError) as caught:
            await vars(mod)["leakcoro"](iter([]))
        self.assertEqual("coroutine raised StopIteration", str(caught.exception))
        self.assertIsInstance(caught.exception.__cause__, StopIteration)


    async def test_koskari_for_each_over_generator(self):
        mod = await self.load("p479_foreach")
        gen = vars(mod)["leakgen"](iter([]))
        result = await vars(mod)["consume"](gen)
        self.assertEqual(
            ["runtime", [1], "generator raised StopIteration", "StopIteration"],
            result)


    async def test_koskari_for_each_stop_async_iteration(self):
        mod = await self.load("p525_foreach")
        agen = vars(mod)["aleakgen"](Empty())
        result = await vars(mod)["aconsume"](agen)
        self.assertEqual(
            ["runtime", [1], "generator raised StopAsyncIteration",
             "StopAsyncIteration"],
            result)


    async def test_provider_source_exhaustion_is_not_converted(self):
        """
        map over a callable that raises StopIteration ends the map: the
        documented truncation, since no generator body is involved
        """

        mod = await self.load("p479_map")
        result = await vars(mod)["mapped"](iter([10, 20]))
        self.assertEqual([10, 20], result)


# The end.
