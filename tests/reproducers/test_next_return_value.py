# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.

"""
tests.reproducers.test_next_return_value

One-argument next on a koskari generator that has finished with a
return value raises a StopIteration carrying that value, as Python's
next does.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'next_return_value.ki')


class TestNextReturnValue(unittest.IsolatedAsyncioTestCase):
    """
    The StopIteration a one-argument next raises past the end of a
    returning generator carries the generator's return value
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.next_return_value')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod

    async def test_stop_iteration_carries_the_return_value(self):
        mod = await self.load_module()
        self.assertEqual(await mod.return_value_of_next(), 5)


# The end.
