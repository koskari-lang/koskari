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
tests.reproducers.test_yield_from_stop_async_payload

A yield-from over an async iterator ends when the iterator raises
StopAsyncIteration; the payload that exception carries is not held by
the delegation once it ends.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import sys
import unittest

import pytest

from koskari.cli.host import run_file_async
from koskari.module import new_module


pytestmark = pytest.mark.phase_reproducers


_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts',
                       'yield_from_stop_async_payload.ki')


class Ending:

    def __init__(self, payload, n=2):
        self.payload = payload
        self.n = n

    def __aiter__(self):
        return self

    async def __anext__(self):
        if self.n:
            self.n -= 1
            return self.n
        raise StopAsyncIteration(self.payload)


class TestYieldFromStopAsyncPayload(unittest.IsolatedAsyncioTestCase):

    async def test_payload_is_not_retained(self):
        mod = new_module('yield_from_stop_async_payload')
        await run_file_async(mod, _SCRIPT)
        run = vars(mod)['run']
        payload = object()

        self.assertEqual([1, 0], await run(Ending(payload)))
        before = sys.getrefcount(payload)
        for _ in range(200):
            await run(Ending(payload))
        self.assertEqual(before, sys.getrefcount(payload))


# The end.
