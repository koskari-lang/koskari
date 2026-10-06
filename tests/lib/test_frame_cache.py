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
tests.lib.test_frame_cache

A function's cached frames: a frame reused from the cache shows no
binding from its earlier life, recursion deeper and shallower than the
cache serves the same answers, the collector walks every cached frame,
and a function freed with a full cache frees them all.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import gc
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define log (list))

(define rec (lambda [n bind]
  (when bind (define x n))
  (when (> n 0) (rec (- n 1) bind))
  (log.append (try x [[NameError] "unbound"]))
  None))

(define fib (lambda [n]
  (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))))

(define depth (lambda [n]
  (if (<= n 0) 0 (+ 1 (depth (- n 1))))))

(define holder (lambda [obj n]
  (define keep obj)
  (when (> n 0) (holder obj (- n 1)))
  keep))
"""


class TestFrameCache(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("frame_cache")
        vars(module)["__file__"] = "<frame_cache>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_reused_frames_carry_no_stale_binding(self):
        mod = await self.load()
        # fill the cache with frames whose x was bound
        await mod["rec"](5, True)
        self.assertEqual(list(mod["log"]), [0, 1, 2, 3, 4, 5])
        mod["log"].clear()
        # the same frames come back for the unbound run
        await mod["rec"](5, False)
        self.assertEqual(list(mod["log"]), ["unbound"] * 6)

    async def test_recursion_around_the_cache_size(self):
        mod = await self.load()
        for n in (3, 7, 8, 9, 16, 40):
            with self.subTest(n=n):
                self.assertEqual(await mod["depth"](n), n)
        self.assertEqual(await mod["fib"](15), 610)
        self.assertEqual(await mod["fib"](15), 610)

    async def test_collector_walks_every_cached_frame(self):
        mod = await self.load()
        payload = object()
        self.assertIs(await mod["holder"](payload, 12), payload)
        gc.collect()
        self.assertIs(await mod["holder"](payload, 3), payload)
        gc.collect()
        self.assertEqual(await mod["depth"](20), 20)

    async def test_function_freed_with_a_full_cache(self):
        mod = await self.load()
        self.assertEqual(await mod["depth"](30), 30)
        del mod["depth"]
        gc.collect()
        self.assertEqual(await mod["fib"](10), 55)


# The end.
