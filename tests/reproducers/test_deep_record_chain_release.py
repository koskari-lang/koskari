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
tests.reproducers.test_deep_record_chain_release

Dropping a long chain of coroutines or async generators -- each one's
bound frame holding the previous one as a local -- releases it with
bounded C stack. A frame returning to its function's cache releases
its locals in place, so no namespace dealloc sits between the links;
a suspended link is closed by its finalizer, which frees the next link
from inside the close. The coroutine and async generator deallocs
defer deep releases through CPython's trashcan, entered before the
finalizer; without it a chain a few tens of thousands long overflowed
the stack.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import unittest

import pytest

from koskari.lib import KosMap
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


DEPTH = 100000

NEST_DEPTH = 300000


SRC = """
(def coro-link [prev] prev)

(def agen-link [prev] (yield prev))

(def suspended-coro-link [prev awaitable]
  (await awaitable)
  prev)
"""


class YieldOnce:
    """
    An awaitable that suspends its awaiter once
    """

    def __await__(self):
        yield None


def load():
    module = new_module("deep_record_chain_release")
    vars(module)["__file__"] = "<deep_record_chain_release>"
    asyncio.run(load_module_source_async(module, SRC))
    return vars(module)


class TestDeepRecordChainRelease(unittest.TestCase):

    def test_unstarted_coroutine_chain(self):
        link = load()["coro-link"]
        c = None
        for _ in range(DEPTH):
            c = link(c)
        del c
        gc.collect()


    def test_unstarted_async_generator_chain(self):
        link = load()["agen-link"]
        g = None
        for _ in range(DEPTH):
            g = link(g)
        del g
        gc.collect()


    def test_suspended_coroutine_chain(self):
        link = load()["suspended-coro-link"]
        c = None
        for _ in range(DEPTH):
            c = link(c, YieldOnce())
            c.send(None)
        del c
        gc.collect()


    def test_suspended_async_generator_chain(self):
        link = load()["agen-link"]
        g = None
        for _ in range(DEPTH):
            g = link(g)
            try:
                g.__anext__().send(None)
            except StopIteration:
                pass
        del g
        gc.collect()


class TestDeepTransformNestRelease(unittest.TestCase):
    """
    A transform over a transform over ..., each dealloc releasing the
    source that is the next, is dropped with bounded C stack
    """

    def test_unstarted_map_nest(self):
        m = iter(())
        for _ in range(NEST_DEPTH):
            m = KosMap(abs, m)
        del m
        gc.collect()


# The end.
