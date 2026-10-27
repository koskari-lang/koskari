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
tests.reproducers.test_yield_from_throw

A throw into a generator suspended in yield-from is thrown into the
delegate, as CPython's yield from does: a delegate that catches it
answers with its next value, one that does not lets it propagate
through the delegating generator with its own cleanup run, and a
close still closes a sync delegate rather than throwing GeneratorExit
into it. An async or Koskari delegate finalizes through the
interpreter's asyncgen hooks, so its cleanup is not asserted here.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(def relay [src] (yield-from src))

(def inner [log]
  (try
    (yield 1)
    ([e ValueError]
      (log.append "inner caught")
      (yield "caught"))
    (finally:
      (log.append "inner finally"))))
"""


def sync_delegate(log):
    try:
        yield 1
    except ValueError:
        log.append("sync caught")
        yield "caught"
    finally:
        log.append("sync finally")


async def async_delegate(log):
    try:
        yield 1
    except ValueError:
        log.append("async caught")
        yield "caught"
    finally:
        log.append("async finally")


def sync_uncaught(log):
    try:
        yield 1
    finally:
        log.append("sync finally")


class TestYieldFromThrow(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("yield_from_throw")
        vars(module)["__file__"] = "<yield_from_throw>"
        await load_module_source_async(module, SRC)
        return vars(module)

    async def test_throw_reaches_sync_generator_delegate(self):
        v = await self.load()
        log = []
        agen = v["relay"](sync_delegate(log))
        self.assertEqual(await agen.__anext__(), 1)
        self.assertEqual(await agen.athrow(ValueError("boom")), "caught")
        self.assertEqual(log, ["sync caught"])
        await agen.aclose()
        self.assertEqual(log, ["sync caught", "sync finally"])

    async def test_throw_reaches_async_generator_delegate(self):
        v = await self.load()
        log = []
        agen = v["relay"](async_delegate(log))
        self.assertEqual(await agen.__anext__(), 1)
        self.assertEqual(await agen.athrow(ValueError("boom")), "caught")
        self.assertEqual(log, ["async caught"])
        await agen.aclose()

    async def test_throw_reaches_koskari_generator_delegate(self):
        v = await self.load()
        log = []
        agen = v["relay"](v["inner"](log))
        self.assertEqual(await agen.__anext__(), 1)
        self.assertEqual(await agen.athrow(ValueError("boom")), "caught")
        self.assertEqual(log, ["inner caught"])
        await agen.aclose()

    async def test_uncaught_throw_propagates_after_delegate_cleanup(self):
        v = await self.load()
        log = []
        agen = v["relay"](sync_uncaught(log))
        self.assertEqual(await agen.__anext__(), 1)
        with self.assertRaises(ValueError):
            await agen.athrow(ValueError("boom"))
        self.assertEqual(log, ["sync finally"])

    async def test_close_closes_the_delegate(self):
        v = await self.load()
        log = []
        agen = v["relay"](sync_delegate(log))
        self.assertEqual(await agen.__anext__(), 1)
        await agen.aclose()
        self.assertEqual(log, ["sync finally"])

    async def test_throw_reaches_a_delegate_two_relays_down(self):
        v = await self.load()
        log = []
        agen = v["relay"](v["relay"](sync_delegate(log)))
        self.assertEqual(await agen.__anext__(), 1)
        self.assertEqual(await agen.athrow(ValueError("boom")), "caught")
        self.assertEqual(log, ["sync caught"])
        await agen.aclose()
        self.assertEqual(log, ["sync caught", "sync finally"])

    async def test_uncaught_throw_two_relays_down_propagates(self):
        v = await self.load()
        log = []
        agen = v["relay"](v["relay"](sync_uncaught(log)))
        self.assertEqual(await agen.__anext__(), 1)
        with self.assertRaises(ValueError):
            await agen.athrow(ValueError("boom"))
        self.assertEqual(log, ["sync finally"])


# The end.
