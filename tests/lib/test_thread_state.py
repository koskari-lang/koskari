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
tests.lib.test_thread_state

The evaluator's per-thread record outlives each entry: repeated
Python-driven entries on one thread, on the demand path and the event
loop path, keep working, threads that run Koskari and exit leave the
process clean, and nested Python domains still account their depth.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import threading
import unittest

import pytest

from koskari.lib import Domain
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define add (lambda [a b] (+ a b)))

(define counter (lambda [n]
  (for-each [i (range n)] (yield i))))
"""


class TestThreadState(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("thread_state")
        vars(module)["__file__"] = "<thread_state>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_repeated_python_driven_entries(self):
        mod = await self.load()
        add = mod["add"]
        for i in range(200):
            self.assertEqual(await add(i, 1), i + 1)
        dgen = iter(mod["counter"](50))
        self.assertEqual(list(dgen), list(range(50)))
        agen = mod["counter"](20)
        seen = [v async for v in agen]
        self.assertEqual(seen, list(range(20)))

    async def test_threads_running_koskari_exit_cleanly(self):
        mod = await self.load()
        add = mod["add"]
        results = {}

        def worker(k):
            results[k] = asyncio.run(add(k, k))
            with Domain():
                results[(k, "demand")] = list(iter(mod["counter"](3)))

        threads = [threading.Thread(target=worker, args=(k,)) for k in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        for k in range(4):
            self.assertEqual(results[k], 2 * k)
            self.assertEqual(results[(k, "demand")], [0, 1, 2])
        # the main thread's own record is untouched by the workers' exits
        self.assertEqual(await add(20, 22), 42)

    async def test_nested_python_domains_keep_their_depth(self):
        mod = await self.load()
        add = mod["add"]
        with Domain():
            self.assertEqual(await add(1, 2), 3)
            with Domain():
                self.assertEqual(await add(2, 3), 5)
            self.assertEqual(await add(3, 4), 7)
        self.assertEqual(await add(4, 5), 9)


# The end.
