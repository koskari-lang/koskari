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
tests.reproducers.test_region_targets_after_seal

Region pushes executed during module load resolve their targets before
the module is sealed, which makes the arena chunks holding the code
read-only. The same sites, and a generator suspended inside a for-each
across the seal, must still arrive at the right instruction afterwards.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(def looped [n]
  (var i 0)
  (var acc 0)
  (while (< i n)
    (incr! i)
    (when (== i 2) (continue))
    (incr! acc i))
  acc)

(def summed [xs]
  (var acc 0)
  (for-each [x xs]
    (when (> x 3) (break))
    (incr! acc x))
  acc)

(def managed [mgr]
  (with [v mgr] (+ v 1)))

(def escaped [x]
  (call/cc (lambda [k] (when x (call k "out")) "in")))

(def shifted [x]
  (reset (+ 1 (shift k (call k x)))))

(def produce [n] (for-each [i (range n)] (yield i)))

;; run every site once during load, before the seal
(var at_load (#list (looped 4) (summed #[1 2 3 4 5]) (escaped True)
                       (shifted 5)))

;; a generator suspended inside its for-each across the seal
(var suspended (produce 5))
(for-each [x suspended] (break))
"""


class Manager:
    def __enter__(self):
        return 10

    def __exit__(self, *exc):
        return False


class TestRegionTargetsAfterSeal(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("region_targets_after_seal")
        vars(module)["__file__"] = "<region_targets_after_seal>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_sites_run_at_load_still_arrive_after_the_seal(self):
        mod = await self.load()
        self.assertEqual(mod["at_load"], [8, 6, "out", 6])
        for _ in range(20):
            self.assertEqual(await mod["looped"](4), 8)
            self.assertEqual(await mod["summed"]([1, 2, 3, 4, 5]), 6)
            self.assertEqual(await mod["managed"](Manager()), 11)
            self.assertEqual(await mod["escaped"](True), "out")
            self.assertEqual(await mod["escaped"](False), "in")
            self.assertEqual(await mod["shifted"](5), 6)

    async def test_generator_suspended_across_the_seal_resumes(self):
        mod = await self.load()
        self.assertEqual([x async for x in mod["suspended"]], [1, 2, 3, 4])


# The end.
