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
tests.reproducers.test_reset_at_layout

reset-at and shift-at written as layout blocks, in module source: the
tag follows the head, the shift-at's continuation name follows the tag,
and the indented block is the body.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(var search (gensym "search"))

def tagged []:
    reset-at search:
        + 1:
            shift-at search k:
                k 5

def crossing []:
    reset-at search:
        + 100:
            reset:
                + 10:
                    shift-at search k:
                        k 1

def collect [choices]:
    shift-at search k:
        var out #[]
        for-each [c choices]:
            out.append (k c)
        out

def in_a_called_function []:
    reset-at search:
        * 2 (collect #[1 2 3])
"""


class TestResetAtLayout(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("reset_at_layout")
        vars(module)["__file__"] = "<reset_at_layout>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_reset_at_and_shift_at_blocks(self):
        mod = await self.load()
        self.assertEqual(6, await mod["tagged"]())


    async def test_shift_at_block_crosses_a_reset_block(self):
        mod = await self.load()
        self.assertEqual(111, await mod["crossing"]())


    async def test_shift_at_in_a_called_function(self):
        mod = await self.load()
        self.assertEqual([2, 4, 6], await mod["in_a_called_function"]())


# The end.
