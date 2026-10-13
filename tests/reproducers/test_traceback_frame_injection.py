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
tests.reproducers.test_traceback_frame_injection

An uncaught Koskari exception reaches Python with the raising Koskari
frame in its traceback: a frame whose code names the Koskari file and
function and whose globals carry the dynchain record, so a stock
traceback shows the Koskari location and the koskari formatter renders
it in place.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import traceback
import unittest

import pytest

from koskari.exceptions import format_exception_with_koskari_trace
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(var boom (lambda boom [] (raise (ValueError "boom"))))
(var outer (lambda outer [] (boom)))
"""


class TestTracebackFrameInjection(unittest.IsolatedAsyncioTestCase):

    async def raise_it(self):
        mod = new_module("tb_inject")
        vars(mod)["__file__"] = "<tb_inject>"
        await load_module_source_async(mod, SRC, layout=False)
        try:
            await vars(mod)["outer"]()
        except ValueError as exc:
            return exc
        self.fail("no exception")

    async def test_raising_koskari_frame_is_in_the_traceback(self):
        exc = await self.raise_it()
        frames = [f for f, _ in traceback.walk_tb(exc.__traceback__)]
        koskari = [f for f in frames if "__koskari_trace__" in f.f_globals]
        self.assertEqual(len(koskari), 1)
        code = koskari[0].f_code
        self.assertEqual(code.co_filename, "<tb_inject>")
        self.assertEqual(code.co_name, "boom")

    async def test_formatter_renders_the_injected_frame(self):
        exc = await self.raise_it()
        text = "".join(format_exception_with_koskari_trace(
            type(exc), exc, exc.__traceback__))
        self.assertIn("<tb_inject>", text)
        self.assertIn("boom", text)

    async def test_formatter_renders_each_frame_once(self):
        exc = await self.raise_it()
        text = "".join(format_exception_with_koskari_trace(
            type(exc), exc, exc.__traceback__))
        self.assertEqual(1, text.count("Koskari dynchain"), text)
        self.assertEqual(1, text.count("in boom"), text)


# The end.
