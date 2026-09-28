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
tests.lib.test_asyncgen_first_step_failure

A generator whose first step is refused, because installing its frame
would exceed the recursion limit, keeps its unstarted frame: once the
stack has unwound it runs from the beginning and yields everything.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import sys
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define produce (lambda [n] (for-each [i (range n)] (yield i))))

(define first_of (lambda [g] (for-each [x g] (return x))))

(define deep (lambda [n g]
  (if (== n 0) (first_of g) (+ 0 (deep (- n 1) g)))))
"""


class TestAsyncGenFirstStepFailure(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("asyncgen_first_step_failure")
        vars(module)["__file__"] = "<asyncgen_first_step_failure>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_generator_survives_a_refused_first_step(self):
        mod = await self.load()
        limit = sys.getrecursionlimit()
        refused = 0
        try:
            sys.setrecursionlimit(80)
            for n in range(60, 80):
                g = mod["produce"](5)
                try:
                    await mod["deep"](n, g)
                except RecursionError:
                    refused += 1
                    self.assertEqual([x async for x in g], [0, 1, 2, 3, 4])
        finally:
            sys.setrecursionlimit(limit)
        self.assertGreater(refused, 0)


# The end.
