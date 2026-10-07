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
tests.reproducers.test_generator_self_close

A generator body that closes the iterator driving it, or an async
generator body that closes the __anext__ awaitable driving it, gets
the refusal Python gives a running generator and carries on; the
adapter it was driven through stays intact.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.cli.host import run_file_async
from koskari.module import new_module


pytestmark = pytest.mark.phase_reproducers


_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts',
                       'generator_self_close.ki')


class TestGeneratorSelfClose(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module('generator_self_close')
        await run_file_async(mod, _SCRIPT)
        return vars(mod)

    async def test_body_closing_its_own_iterator(self):
        ns = await self.load()
        box = [None]
        it = iter(ns['g'](box))
        box[0] = it
        self.assertEqual(1, next(it))
        self.assertEqual(2, next(it))
        self.assertEqual(1, len(box[1:]))
        self.assertIn("already executing", box[1])

    async def test_body_closing_its_own_anext_awaitable(self):
        ns = await self.load()
        box = [None]
        agen = ns['g'](box)
        self.assertEqual(1, await agen.__anext__())
        step = agen.__anext__()
        box[0] = step
        self.assertEqual(2, await step)
        self.assertEqual(1, len(box[1:]))
        self.assertIn("already executing", box[1])


# The end.
