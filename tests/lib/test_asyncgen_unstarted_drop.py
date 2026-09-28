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
tests.lib.test_asyncgen_unstarted_drop

Dropping an async generator that was never started releases its frame
and its GEN node: creating and dropping many of them does not grow the
memory the allocator traces.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import gc
import tracemalloc
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define produce (lambda [n] (for-each [i (range n)] (yield i))))
"""


class TestAsyncGenUnstartedDrop(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("asyncgen_unstarted_drop")
        vars(module)["__file__"] = "<asyncgen_unstarted_drop>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_dropping_unstarted_generators_does_not_grow_memory(self):
        mod = await self.load()
        produce = mod["produce"]

        def churn(count):
            for _ in range(count):
                g = produce(3)
                del g

        churn(200)
        gc.collect()
        tracemalloc.start()
        try:
            before = tracemalloc.get_traced_memory()[0]
            churn(2000)
            gc.collect()
            after = tracemalloc.get_traced_memory()[0]
        finally:
            tracemalloc.stop()
        # a leaked GEN node is over 200 bytes each; allow allocator noise
        self.assertLess(after - before, 2000 * 50)


# The end.
