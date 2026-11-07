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
tests.lib.test_demand_cached_context_threads

One demand wrapper called from several threads at once: each call runs
on a drive context of its own, so every call returns its own value even when
the callee releases the GIL mid-call.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import sys
import threading
import time
import unittest

import pytest

import koskari.lib as lib
from koskari.module import load_module_source_async, new_module
from tests import needs_threads


pytestmark = pytest.mark.phase_lib


SRC = """
(define [via release x]
  (release)
  (* x x))
"""


def release():
    time.sleep(0)


@needs_threads
class TestDemandCachedContextThreads(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("demand_cached_context_threads")
        vars(module)["__file__"] = "<demand_cached_context_threads>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)


    async def test_threads_sharing_one_wrapper_get_their_own_results(self):
        mod = await self.load()
        d = lib.demand(mod["via"])
        rounds = 400
        results = {}
        errors = []

        def worker(base):
            out = []
            try:
                for i in range(rounds):
                    x = base + i
                    out.append((x, d(release, x)))
            except BaseException as exc:
                errors.append(exc)
            results[base] = out

        old = sys.getswitchinterval()
        sys.setswitchinterval(1e-6)
        try:
            threads = [threading.Thread(target=worker, args=(b,))
                       for b in (1000, 2000, 3000)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()
        finally:
            sys.setswitchinterval(old)

        self.assertEqual(errors, [])
        for base, out in results.items():
            self.assertEqual(len(out), rounds)
            for x, got in out:
                self.assertEqual(got, x * x)


# The end.
