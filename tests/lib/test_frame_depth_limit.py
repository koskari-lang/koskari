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
tests.lib.test_frame_depth_limit

The process-wide frame depth limit is 10000 by default and settable
from 1 up to the 65535 cap through koskari.lib.get_frame_depth_limit
and koskari.lib.set_frame_depth_limit. A frame installed past it raises
RecursionError naming the limit in force.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import get_frame_depth_limit, set_frame_depth_limit
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define [deep n]
  (if (== n 0) 0 (+ 1 (deep (- n 1)))))
"""


class TestFrameDepthLimitAccess(unittest.TestCase):

    def setUp(self):
        self.saved = get_frame_depth_limit()


    def tearDown(self):
        set_frame_depth_limit(self.saved)


    def test_default(self):
        self.assertEqual(get_frame_depth_limit(), 10000)


    def test_round_trip(self):
        for n in (1, 80, 10000, 65535):
            with self.subTest(n=n):
                self.assertIsNone(set_frame_depth_limit(n))
                self.assertEqual(get_frame_depth_limit(), n)


    def test_out_of_range_refused(self):
        set_frame_depth_limit(500)
        for n in (0, 65536, -1, 2 ** 80):
            with self.subTest(n=n):
                with self.assertRaises(ValueError):
                    set_frame_depth_limit(n)
                self.assertEqual(get_frame_depth_limit(), 500)


    def test_non_int_refused(self):
        set_frame_depth_limit(500)
        for n in (500.0, "500", None):
            with self.subTest(n=n):
                with self.assertRaises(TypeError):
                    set_frame_depth_limit(n)
                self.assertEqual(get_frame_depth_limit(), 500)


class TestFrameDepthLimitInForce(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("frame_depth_limit")
        vars(module)["__file__"] = "<frame_depth_limit>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)


    async def test_lowered_limit_in_message(self):
        mod = await self.load()
        saved = get_frame_depth_limit()
        try:
            set_frame_depth_limit(100)
            self.assertEqual(await mod["deep"](50), 50)
            with self.assertRaisesRegex(
                    RecursionError,
                    r"^Koskari frame depth exceeded \(100\)$"):
                await mod["deep"](200)
        finally:
            set_frame_depth_limit(saved)


# The end.
