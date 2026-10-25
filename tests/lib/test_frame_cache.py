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
and a function freed with a full cache frees them all. Calls of every
shape (keywords, consulted defaults, star, keyword-only, starstar,
bound methods, a class's __init__) repeated through one function give
the answers a fresh frame would, and a call that fails to bind leaves
the next call unaffected.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import gc
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(var log (list))

(define [rec n bind]
  (when bind (var x n))
  (when (> n 0) (rec (- n 1) bind))
  (log.append (try x [[NameError] "unbound"]))
  None)

(define [fib n]
  (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(define [depth n]
  (if (<= n 0) 0 (+ 1 (depth (- n 1)))))

(define [holder obj n]
  (var keep obj)
  (when (> n 0) (holder obj (- n 1)))
  keep)
"""


GENERAL_SRC = """
(def kw [a b: 0] #(a b))
(def star [a *: rest] #(a rest))
(def kwonly [a *: rest k: 1] #(a rest k))
(def opts [a **: more] #(a more))

(def depthkw [n step: 1]
  (if (<= n 0) 0 (+ 1 (depthkw (- n step) step: step))))

(def run_kw []
  #((kw 1 b: 2) (kw 3) (kw a: 4 b: 5) (kw 6) (kw b: 7 a: 8)))

(def run_star []
  #((star 1 2 3) (star 4) (star 5 6) (star 7)))

(def run_kwonly []
  #((kwonly 1 2 k: 5) (kwonly 3) (kwonly 4 5 6) (kwonly 7 k: 8)))

(def run_opts []
  #((opts 1 x: 2) (opts 3) (opts 4 y: 5 z: 6) (opts 7)))

(def run_bad []
  #((kw 1 b: 2)
    (try (kw 10 b: 5 nope: 1) [[TypeError] "bad"])
    (kw 1)
    (try (kw b: 3) [[TypeError] "missing"])
    (kw 2)
    (star 1 2)
    (try (star 3 4 nope: 1) [[TypeError] "bad"])
    (star 5)))

(class Scaler []
  (begin
    (def __init__ [self base: 1] (set-attr self base base))
    (def scale [self x factor: 1] (* self.base x factor))))

(def run_method []
  (var s (Scaler base: 2))
  #((s.scale 3 factor: 4) (s.scale 5) (s.scale x: 6 factor: 7) (s.scale 8)))

(class Point []
  (begin
    (def __init__ [self x y: 0 *: more]
      (set-attr self x x)
      (set-attr self y y)
      (set-attr self more more))))

(def coords [p] #(p.x p.y p.more))

(var box #{"v": 1})
(var inner (list))
(def byref [a &n: 0 b: 0] #(a n b))
(var box-ref &(item box "v"))

(def empty-check []
  (when (not inner)
    (inner.append (byref 100 n: box-ref b: 200)))
  False)

(bind! reentering (dynamic { (item box "v") } { (set! (item box "v") %1) }
                           { None } empty-check))

(def run_reenter []
  #((byref 1 n: box-ref b: 2) (byref 3 n: &reentering b: 4)
    (byref 5 n: box-ref b: 6)))

(def run_init []
  #((coords (Point 1 y: 2)) (coords (Point 3)) (coords (Point x: 4 y: 5))
    (coords (Point 6 7 8 9)) (coords (Point 10))))
"""


CONT_SRC = """
(var saved (list))

(def capture [n step: 1]
  (var local (* n step))
  (+ local (shift k (begin (saved.append k) 0))))

(def run_capture []
  #((reset (capture 100)) (reset (capture 200 step: 2))
    (reset (capture 7)) (reset (capture 8 step: 3))))

(def resume_saved []
  #((call (item saved 0) 1) (call (item saved 1) 2)
    (call (item saved 0) 5)))
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


class TestFrameCacheGeneralCalls(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("frame_cache_general")
        vars(module)["__file__"] = "<frame_cache_general>"
        await load_module_source_async(module, GENERAL_SRC, layout=False)
        return vars(module)


    async def assert_repeats(self, mod, name, expected):
        # the second run is served by frames the first run cached
        for run in (1, 2):
            with self.subTest(name=name, run=run):
                self.assertEqual(await mod[name](), expected)


    async def test_keyword_and_default_calls(self):
        mod = await self.load()
        await self.assert_repeats(
            mod, "run_kw", ((1, 2), (3, 0), (4, 5), (6, 0), (8, 7)))


    async def test_star_calls(self):
        mod = await self.load()
        await self.assert_repeats(
            mod, "run_star", ((1, (2, 3)), (4, ()), (5, (6,)), (7, ())))


    async def test_keyword_only_calls(self):
        mod = await self.load()
        await self.assert_repeats(
            mod, "run_kwonly",
            ((1, (2,), 5), (3, (), 1), (4, (5, 6), 1), (7, (), 8)))


    async def test_starstar_calls(self):
        mod = await self.load()
        await self.assert_repeats(
            mod, "run_opts",
            ((1, {"x": 2}), (3, {}), (4, {"y": 5, "z": 6}), (7, {})))


    async def test_failed_bind_then_good_call(self):
        mod = await self.load()
        await self.assert_repeats(
            mod, "run_bad",
            ((1, 2), "bad", (1, 0), "missing", (2, 0),
             (1, (2,)), "bad", (5, ())))


    async def test_bound_method_with_keywords(self):
        mod = await self.load()
        await self.assert_repeats(mod, "run_method", (24, 10, 84, 16))


    async def test_init_with_keywords(self):
        mod = await self.load()
        await self.assert_repeats(
            mod, "run_init",
            ((1, 2, ()), (3, 0, ()), (4, 5, ()), (6, 7, (8, 9)),
             (10, 0, ())))


    async def test_call_from_inside_the_bind(self):
        # the floor test on the dynamic ref calls byref again while the
        # outer call is binding its arguments
        mod = await self.load()
        self.assertEqual(await mod["run_reenter"](),
                         ((1, 1, 2), (3, 1, 4), (5, 1, 6)))
        self.assertEqual(list(mod["inner"]), [(100, 1, 200)])


    async def test_keyword_recursion_around_the_cache_size(self):
        mod = await self.load()
        for n in (3, 7, 8, 9, 16, 40, 5):
            with self.subTest(n=n):
                self.assertEqual(await mod["depthkw"](n), n)


class TestFrameCacheContinuations(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("frame_cache_cont")
        vars(module)["__file__"] = "<frame_cache_cont>"
        await load_module_source_async(module, CONT_SRC, layout=False)
        return vars(module)


    async def test_captured_frame_is_not_reused(self):
        # later calls of capture must not take a frame a saved
        # continuation still holds, or the captured local is reset
        mod = await self.load()
        self.assertEqual(await mod["run_capture"](), (0, 0, 0, 0))
        gc.collect()
        self.assertEqual(await mod["resume_saved"](), (101, 402, 105))


# The end.
