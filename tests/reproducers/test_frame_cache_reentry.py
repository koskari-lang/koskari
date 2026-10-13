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
tests.reproducers.test_frame_cache_reentry

Caching a returned frame clears its locals, which can run a finalizer
that calls the same function again and takes frames out of the cache.
The frame must be linked into the cache as it is after that, not as
it was before.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import gc
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(var fn (lambda [flag obj]
  (if (== flag 2)
      (fn 0 None)
      (if (== flag 1) (lambda [] flag) None))))

(var call_with (lambda [make] (fn 0 (make))))
"""


class TestFrameCacheReentry(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("frame_park_reentry")
        vars(module)["__file__"] = "<frame_park_reentry>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_finalizer_reentering_the_function_during_the_cache(self):
        mod = await self.load()
        fn = mod["fn"]
        seen = []

        class D:
            def __del__(self):
                seen.append(fn(1, None).demand()().demand())

        # two live activations cache two frames
        self.assertIsNone(await fn(2, None))
        # the cached frame's only reference to the D instance dies when
        # the frame is cached; its finalizer calls fn from Python and
        # takes a cached frame, returning a closure that escapes
        self.assertIsNone(await mod["call_with"](D))
        gc.collect()
        self.assertEqual(seen, [1])
        # the cache is still usable afterwards
        for _ in range(10):
            self.assertIsNone(await fn(0, None))
            self.assertIsNone(await fn(2, None))
        self.assertEqual(await (await fn(1, None))(), 1)


# The end.
