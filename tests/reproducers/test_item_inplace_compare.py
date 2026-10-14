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
tests.reproducers.test_item_inplace_compare

Full-stack reproducer for the first run of an in-place item update on
an exact dict under a key equal to, but not identical with, the stored
key: __eq__ runs as often as in Python's d[k] += 1, and an exception
from __eq__ propagates with the cell unchanged.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import os
import unittest

import pytest

from koskari.lib import OpCode
from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'item_inplace_compare.ki')


def opcodes_of(fn):
    code = fn.code
    return [OpCode(code.oparg(i)[0]) for i in range(code.length())]


class Key:
    """
    A key with a fixed hash whose __eq__ records each call, and raises
    on the call numbered raise_on
    """

    def __init__(self, calls, raise_on=None):
        self.calls = calls
        self.raise_on = raise_on


    def __hash__(self):
        return 7


    def __eq__(self, other):
        self.calls.append(other)
        if len(self.calls) == self.raise_on:
            raise ValueError("compare failed")
        return True


def make_keys(raise_on=None):
    calls = []
    return calls, Key(calls, raise_on), Key(calls, raise_on)


class TestItemInplaceCompare(unittest.IsolatedAsyncioTestCase):

    async def load_module(self):
        mod = new_module('tests.reproducers.item_inplace_compare')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_site_is_an_item_inplace_opcode(self):
        mod = await self.load_module()
        self.assertIn(OpCode.FAST_ITEM_FAST_INPLACE_CONST,
                      opcodes_of(mod.bump))


    async def test_raising_compare_propagates(self):
        mod = await self.load_module()

        calls, a, b = make_keys(raise_on=1)
        d = {a: 1}
        with self.assertRaises(ValueError):
            d[b] += 1
        self.assertEqual(d[a], 1)

        calls, a, b = make_keys(raise_on=1)
        d = {a: 1}
        with self.assertRaises(ValueError):
            await mod.bump(d, b)
        self.assertEqual(len(calls), 1)
        self.assertEqual(d, {a: 1})


    async def test_compare_count_matches_python(self):
        mod = await self.load_module()

        calls, a, b = make_keys()
        expected = {a: 1}
        expected[b] += 1
        expected_count = len(calls)

        calls, a, b = make_keys()
        d = {a: 1}
        self.assertIs(await mod.bump(d, b), d)
        self.assertEqual(len(calls), expected_count)
        self.assertEqual(list(d.values()), list(expected.values()))


# The end.
