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
tests.lib.opcode.test_region_targets

Region pushes resolve their break, continue, resume, and handler
targets once per site and keep using them: the same loop, for-each,
try, with, call/cc, and reset sites re-entered many times, from one
function and from two generators interleaved on one code object, land
where they should every time.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define nested (lambda [n]
  (define total 0)
  (define i 0)
  (while (< i n)
    (incr! i)
    (when (== (% i 3) 0) (continue))
    (define j 0)
    (while True
      (incr! j)
      (when (> j 2) (break))
      (incr! total (* i j))))
  total))

(define foreach_sum (lambda [xs limit]
  (define total 0)
  (for-each [x xs]
    (when (== x 1) (continue))
    (when (> x limit) (break))
    (incr! total x))
  total))

(define first_over (lambda [xs limit]
  (for-each [x xs] (when (> x limit) (break x)))))

(define guarded (lambda [x]
  (try
    (if (== x 0) (raise (ValueError "zero")) x)
    ([e ValueError] -1))))

(define managed (lambda [mgr x]
  (with [v mgr] (+ v x))))

(define escape (lambda [x]
  (call/cc (lambda [k] (when (> x 5) (call k "big")) "small"))))

(define shifted (lambda [x]
  (reset (* 2 (shift k (call k x))))))

(define counting (lambda [n]
  (define i 0)
  (while (< i n)
    (incr! i)
    (define j 0)
    (while (< j 2)
      (incr! j)
      (yield (+ (* 10 i) j))))))
"""


class Manager:
    def __init__(self, value):
        self.value = value
        self.exits = 0

    def __enter__(self):
        return self.value

    def __exit__(self, *exc):
        self.exits += 1
        return False


class TestRegionTargets(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("region_targets")
        vars(module)["__file__"] = "<region_targets>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_nested_loops_reentered_many_times(self):
        mod = await self.load()
        expected = sum(i * j for i in range(1, 8) if i % 3 for j in (1, 2))
        for _ in range(50):
            self.assertEqual(await mod["nested"](7), expected)

    async def test_for_each_break_and_continue_reentered(self):
        mod = await self.load()
        for _ in range(50):
            self.assertEqual(await mod["foreach_sum"]([1, 2, 3, 9, 4], 5), 5)
        self.assertEqual(await mod["foreach_sum"]([], 5), 0)

    async def test_valued_break_is_the_loop_value(self):
        mod = await self.load()
        for _ in range(20):
            self.assertEqual(await mod["first_over"]([1, 2, 7, 3], 4), 7)
        self.assertIsNone(await mod["first_over"]([1, 2], 4))

    async def test_try_handler_taken_after_reentry(self):
        mod = await self.load()
        for _ in range(20):
            self.assertEqual(await mod["guarded"](3), 3)
            self.assertEqual(await mod["guarded"](0), -1)

    async def test_with_exit_resumes_after_body(self):
        mod = await self.load()
        mgr = Manager(10)
        for n in range(20):
            self.assertEqual(await mod["managed"](mgr, n), 10 + n)
        self.assertEqual(mgr.exits, 20)

    async def test_call_cc_escape_reentered(self):
        mod = await self.load()
        for _ in range(20):
            self.assertEqual(await mod["escape"](9), "big")
            self.assertEqual(await mod["escape"](1), "small")

    async def test_reset_shift_reentered(self):
        mod = await self.load()
        for x in range(20):
            self.assertEqual(await mod["shifted"](x), 2 * x)

    async def test_two_generators_interleave_on_one_code_object(self):
        mod = await self.load()
        a = mod["counting"](3)
        b = mod["counting"](2)
        seen = []
        seen.append(await a.__anext__())
        seen.append(await b.__anext__())
        seen.append(await a.__anext__())
        seen.append(await b.__anext__())
        seen.append(await a.__anext__())
        seen.append(await b.__anext__())
        self.assertEqual(seen, [11, 11, 12, 12, 21, 21])
        rest_a = [x async for x in a]
        rest_b = [x async for x in b]
        self.assertEqual(rest_a, [22, 31, 32])
        self.assertEqual(rest_b, [22])


# The end.
