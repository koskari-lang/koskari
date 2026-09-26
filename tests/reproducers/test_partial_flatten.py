# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
tests.reproducers.test_partial_flatten

Full-stack reproducer: a partial called from Koskari runs its inner
callable through the evaluator for every kind of inner.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import asyncio
import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'partial_flatten.ki')


def py_add(a, b):
    """
    Plain Python two-argument callable
    """

    return a + b


def py_kw(a, b=0, c=0):
    """
    Plain Python callable taking keywords
    """

    return (a, b, c)


async def py_async(a, b):
    """
    Python coroutine function
    """

    await asyncio.sleep(0)
    return a + b


class TestPartialFlatten(unittest.IsolatedAsyncioTestCase):
    """
    partial called from Koskari bytecode
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.partial_flatten')
        ns = vars(mod)
        ns['py_add'] = py_add
        ns['py_kw'] = py_kw
        ns['py_async'] = py_async
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def call(self, name):
        mod = await self.load_module()
        return await vars(mod)[name]()


    async def test_demand_inner_flattens(self):
        self.assertEqual(await self.call('via-demand-partial'), (7, 10))


    async def test_async_python_inner_is_awaited(self):
        self.assertEqual(await self.call('via-async-partial'), 3)


    async def test_koskari_bound_keywords(self):
        self.assertEqual(await self.call('kos-bound-kw'), (1, 2, 3))


    async def test_koskari_call_keywords(self):
        self.assertEqual(await self.call('kos-call-kw'), (1, 2, 3))


    async def test_koskari_both_keywords(self):
        self.assertEqual(await self.call('kos-both-kw'), (1, 2, 3))


    async def test_python_bound_keywords(self):
        self.assertEqual(await self.call('py-bound-kw'), (1, 2, 3))


    async def test_python_both_keywords(self):
        self.assertEqual(await self.call('py-both-kw'), (1, 2, 3))


    async def test_python_inner_in_tail_position(self):
        self.assertEqual(await self.call('py-tail'), 42)


# The end.
