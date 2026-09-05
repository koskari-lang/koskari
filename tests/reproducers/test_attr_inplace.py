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
tests.reproducers.test_attr_inplace

Full-stack reproducer for attribute updates through the electors: the
churn field kernels' loop on a dict-backed class and on a __slots__
class matches the same arithmetic in Python, with ints and with
floats, and a value the outside still holds is never rewritten.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.lib import OpCode
from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'attr_inplace.ki')

MASK32 = 0xffffffff


def opcodes_of(fn):
    code = fn.code
    return [OpCode(code.oparg(i)[0]) for i in range(code.length())]


def churn_py(n, x):
    i, a, b = 0, x & MASK32, 1
    while i < n:
        a = a + b
        a = a & MASK32
        b = b ^ a
        i = i + 1
    return (a ^ b) & MASK32


def churn_float_py(n):
    i, a, b = 0, 1.5, 0.25
    while i < n:
        a = a * 1.0001
        b = b + a
        a = a - (b / 3.0)
        i = i + 1
    return (a, b)


class TestAttrInplace(unittest.IsolatedAsyncioTestCase):

    async def load_module(self):
        mod = new_module('tests.reproducers.attr_inplace')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_int_churn_on_both_classes(self):
        mod = await self.load_module()
        self.assertIn(OpCode.FAST_ATTR_INPLACE, opcodes_of(mod.churn))
        for n, x in ((0, 1), (1, 1), (500, 1), (2000, 12345)):
            self.assertEqual(await mod.churn_dict(n, x), churn_py(n, x))
            self.assertEqual(await mod.churn_slots(n, x), churn_py(n, x))


    async def test_float_churn_on_both_classes(self):
        mod = await self.load_module()
        for n in (0, 1, 300, 1000):
            expected = churn_float_py(n)
            self.assertEqual(await mod.churn_float_dict(n), expected)
            self.assertEqual(await mod.churn_float_slots(n), expected)


    async def test_shared_value_is_untouched(self):
        mod = await self.load_module()
        for n in (7, 300, 2 ** 40):
            seed, a = await mod.shared_value(n)
            self.assertEqual(seed, n * 1000)
            self.assertEqual(a, n * 1000 + 2)


# The end.
