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
tests.reproducers.test_trailing_return_position

A function whose last form is a return that the processor fuses with
its feeder reports the return's line for an error raised while that
return's operands are evaluated: the finished rows end with an
instruction, never with a position row the position table cannot
place.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import traceback
import unittest

import pytest

from koskari.compile.processor import ByteCodeProcessor
from koskari.lib import Code, NamespaceSpec, OpCode
from koskari.module import load_module_source_async, new_module

from tests.compile import make_fragment


pytestmark = pytest.mark.phase_reproducers


FILENAME = "/nonexistent/trailing_return_position.ki"


def callee_lines(exc, name):
    entries = [frame.f_globals.get("__koskari_trace__")
               for frame, _ in traceback.walk_tb(exc.__traceback__)]
    return sorted(set(
        e.get("line") for e in entries
        if isinstance(e, dict) and e.get("name") == name))


class TestTrailingReturnPosition(unittest.IsolatedAsyncioTestCase):

    async def error_for(self, src):
        module = new_module("trailing_return_position")
        vars(module)["__file__"] = FILENAME
        # caught here rather than by assertRaises, which clears the
        # traceback the lines are read from
        try:
            await load_module_source_async(module, src)
        except ValueError as exc:
            return exc
        self.fail("ValueError not raised")


    async def test_return_tuple_last(self):
        err = await self.error_for(
            "def parse [text]:\n"
            "    return (int text)\n"
            "\n"
            "def load [a b]:\n"
            "    define n 1\n"
            "    return #(n (parse a) (parse b))\n"
            "\n"
            "(load \"1\" \"x\")\n")
        self.assertEqual([6], callee_lines(err, "load"))


class TestFinishedRowsEndWithInstruction(unittest.TestCase):

    def test_fused_return_drops_trailing_position(self):
        rows = [
            (OpCode.POSITION, (6, 4)),
            (OpCode.FAST, "n"),
            (OpCode.FAST, "m"),
            (OpCode.BUILD_ARGS, 2),
            (OpCode.POSITION, (6, 11)),
            (OpCode.RETURN, 1),
        ]
        out = ByteCodeProcessor(make_fragment(rows)).process()
        self.assertNotEqual(OpCode.POSITION, out[-1][0])


    def test_trailing_position_row_is_not_placed(self):
        nspec = NamespaceSpec(None, (), ())
        code = Code(nspec, (
            [OpCode.POSITION, (6, 4)],
            [OpCode.CONST, 1],
            [OpCode.RETURN, 1],
            [OpCode.POSITION, (6, 11)],
        ))
        self.assertEqual(2, code.length())
        self.assertEqual((6, 4), code.position(1))


# The end.
