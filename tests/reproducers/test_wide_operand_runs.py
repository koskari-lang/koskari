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
tests.reproducers.test_wide_operand_runs

A form that pushes a few hundred operands before the instruction that
consumes them -- a wide call, a wide computed list or tuple, a wide
quoted list, a wide dict literal -- evaluates. The operand segment
grows to hold them; its limit is above anything a single instruction
the finalizer accepts can ask for.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


WIDTH = 300


def source():
    args = " ".join(str(i) for i in range(WIDTH))
    computed = " ".join(f"(+ {i} 0)" for i in range(WIDTH))
    entries = " ".join(f'"k{i}": (+ {i} 0)' for i in range(WIDTH))
    return f"""
def wide-call []:
    (max {args})

def wide-list []:
    #[{computed}]

def wide-tuple []:
    #({computed})

def wide-quoted []:
    var p '({args})
    (p.length)

def wide-dict []:
    (len #{{{entries}}})
"""


class TestWideOperandRuns(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("wide")
        vars(module)["__file__"] = "<wide>"
        await load_module_source_async(module, source())
        return vars(module)


    async def test_wide_call(self):
        m = await self.load()
        self.assertEqual(WIDTH - 1, await m["wide-call"]())


    async def test_wide_computed_list_and_tuple(self):
        m = await self.load()
        self.assertEqual(list(range(WIDTH)), await m["wide-list"]())
        self.assertEqual(tuple(range(WIDTH)), await m["wide-tuple"]())


    async def test_wide_quoted_list(self):
        m = await self.load()
        self.assertEqual(WIDTH, await m["wide-quoted"]())


    async def test_wide_dict_literal(self):
        m = await self.load()
        self.assertEqual(WIDTH, await m["wide-dict"]())


# The end.
