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
tests.reproducers.test_iter_function

Full-stack reproducer: a generator-function __iter__ is wrapped in
IterFunction, presenting an iterator to Python and the generator to
Koskari.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import os
import unittest

import pytest

from koskari.lib import AsyncGenerator
from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'iter_function.ki')


class TestIterFunction(unittest.IsolatedAsyncioTestCase):
    """
    __iter__ bound to a generator function
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.iter_function')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_generator_iter_is_wrapped(self):
        mod = await self.load_module()
        counter = vars(mod)['Counter']
        self.assertEqual(
            type(counter.__dict__['__iter__']).__name__, 'IterFunction')


    async def test_python_iter_presents_iterator(self):
        mod = await self.load_module()
        obj = vars(mod)['Counter']()
        it = iter(obj)
        self.assertEqual(type(it).__name__, 'Iterator')
        self.assertIs(iter(it), it)
        self.assertEqual(next(it), 1)
        self.assertEqual(list(obj), [1, 2])


    async def test_koskari_call_returns_generator(self):
        mod = await self.load_module()
        result = await vars(mod)['koskari-iter-call']()
        self.assertIsInstance(result, AsyncGenerator)


    async def test_koskari_for_each(self):
        mod = await self.load_module()
        self.assertEqual(await vars(mod)['koskari-for-each'](), [1, 2])


    async def test_plain_iter_stays_demand(self):
        mod = await self.load_module()
        plain = vars(mod)['Plain']
        self.assertEqual(type(plain.__dict__['__iter__']).__name__, 'demand')
        self.assertEqual(list(plain()), [1, 2])


# The end.
