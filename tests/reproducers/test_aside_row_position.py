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
tests.reproducers.test_aside_row_position

A reset, shift, abort or dyn-wind written over several lines reports
the line the form starts on: for an error the form itself raises, and
for the frame the form is written in while its body, handler or
segment runs in a frame of its own.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import traceback
import unittest

import pytest

from koskari.exceptions import KoskariPromptNotFound
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


FILENAME = "/nonexistent/aside_row_position.ki"


SRC = (
    "def reset_raises []:\n"                        # 1
    "    (reset\n"                                  # 2
    "        (raise (ValueError \"body\"))\n"       # 3
    "        (list))\n"                             # 4
    "\n"                                            # 5
    "def shift_unreachable []:\n"                   # 6
    "    (shift k\n"                                # 7
    "        (list)\n"                              # 8
    "        (k 2))\n"                              # 9
    "\n"                                            # 10
    "def abort_unreachable []:\n"                   # 11
    "    (abort\n"                                  # 12
    "        (list)\n"                              # 13
    "        (list))\n"                             # 14
    "\n"                                            # 15
    "def before_raises []:\n"                       # 16
    "    (dyn-wind\n"                               # 17
    "        (raise (ValueError \"before\"))\n"     # 18
    "        (list)\n"                              # 19
    "        (begin\n"                              # 20
    "            (list)\n"                          # 21
    "            (list)))\n"                        # 22
)


def callee_lines(exc, name):
    entries = [frame.f_globals.get("__koskari_trace__")
               for frame, _ in traceback.walk_tb(exc.__traceback__)]
    return sorted(set(
        e.get("line") for e in entries
        if isinstance(e, dict) and e.get("name") == name))


class TestAsideRowPosition(unittest.IsolatedAsyncioTestCase):

    async def lines_for(self, name, error):
        module = new_module("aside_row_position")
        vars(module)["__file__"] = FILENAME
        await load_module_source_async(module, SRC)
        # caught here rather than by assertRaises, which clears the
        # traceback the lines are read from
        try:
            await vars(module)[name]()
        except error as exc:
            return callee_lines(exc, name)
        self.fail(f"{error.__name__} not raised")


    async def test_reset_reports_its_own_line_under_its_body(self):
        self.assertEqual(
            [2, 3], await self.lines_for("reset_raises", ValueError))


    async def test_shift_without_a_prompt_reports_its_own_line(self):
        self.assertEqual(
            [7],
            await self.lines_for("shift_unreachable", KoskariPromptNotFound))


    async def test_abort_without_a_prompt_reports_its_own_line(self):
        self.assertEqual(
            [12],
            await self.lines_for("abort_unreachable", KoskariPromptNotFound))


    async def test_dyn_wind_reports_its_own_line_under_before(self):
        self.assertEqual(
            [17, 18], await self.lines_for("before_raises", ValueError))


# The end.
