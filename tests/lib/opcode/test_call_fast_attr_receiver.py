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
tests.lib.opcode.test_call_fast_attr_receiver

A method call on a fast local receiver: the receiver's reference count
is the same after any number of calls, on the cached-frame hit, on a
receiver whose class resolves attributes in user code, and on a method
the simple path does not take; the method sees the receiver as self; a
receiver held only by the local survives its own call.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import sys
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(class Counter []
  (define [__init__ self] (set-attr self count 0))
  (define [bump self] (set-attr self count (+ self.count 1)) self)
  (define [bump_by self n: 1] (set-attr self count (+ self.count n)) self))

(class Lazy []
  (define [__init__ self] (set-attr self count 0))
  (define [__getattr__ self name]
    (if (== name "bump")
        (lambda [] (set-attr self count (+ self.count 1)) self)
        (raise (AttributeError name)))))

(define drive (lambda [o n]
  (define i 0)
  (define last None)
  (while (< i n)
    (set! last (o.bump))
    (incr! i))
  last))

(define drive_default (lambda [o n]
  (define i 0)
  (while (< i n)
    (o.bump_by)
    (incr! i))
  o.count))

(define local_only (lambda [make]
  (define o (make))
  (o.bump)
  (o.bump)
  o.count))
"""


class TestCallFastAttrReceiver(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("call_fast_attr_receiver")
        vars(module)["__file__"] = "<call_fast_attr_receiver>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_receiver_refcount_is_stable_on_the_cached_path(self):
        mod = await self.load()
        o = mod["Counter"]()
        await mod["drive"](o, 5)
        before = sys.getrefcount(o)
        last = await mod["drive"](o, 500)
        self.assertIs(last, o)
        del last
        self.assertEqual(sys.getrefcount(o), before)
        self.assertEqual(o.count, 505)

    async def test_receiver_refcount_is_stable_through_getattr(self):
        mod = await self.load()
        o = mod["Lazy"]()
        await mod["drive"](o, 3)
        before = sys.getrefcount(o)
        last = await mod["drive"](o, 200)
        self.assertIs(last, o)
        del last
        self.assertEqual(sys.getrefcount(o), before)
        self.assertEqual(o.count, 203)

    async def test_receiver_refcount_is_stable_on_the_general_path(self):
        mod = await self.load()
        o = mod["Counter"]()
        await mod["drive_default"](o, 3)
        before = sys.getrefcount(o)
        self.assertEqual(await mod["drive_default"](o, 200), 203)
        self.assertEqual(sys.getrefcount(o), before)

    async def test_receiver_held_only_by_the_local_survives_its_calls(self):
        mod = await self.load()
        for _ in range(20):
            self.assertEqual(await mod["local_only"](mod["Counter"]), 2)


# The end.
