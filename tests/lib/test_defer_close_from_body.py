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
tests.lib.test_defer_close_from_body

A coroutine driven through a defer closes that defer from inside its
own body. The close is refused by the coroutine's re-entry guard and
spends the defer, which drops the defer's reference to the running
coroutine; the step in flight must still complete on a live object.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

import koskari.lib as lib
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define [body box]
  (define d (item box 0))
  (try
    (d.close)
    ([e RuntimeError] "refused")))
"""


class TestDeferCloseFromBody(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("defer_close_from_body")
        vars(module)["__file__"] = "<defer_close_from_body>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_body_closing_its_own_defer(self):
        mod = await self.load()
        for _ in range(20):
            box = [None]
            d = lib.defer(mod["body"], box)
            box[0] = d
            self.assertEqual(await d, "refused")
            box[0] = None


# The end.
