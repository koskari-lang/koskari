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
tests.reproducers.test_stack_drop_gc

A finalizer that runs while the evaluator drops a window of stack
slots may allocate and trigger a collection; the collector then
traverses the running context, and the stack it visits must not
cover the slots already released.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import gc
import os
import unittest

import pytest

from koskari.cli.host import run_file_async
from koskari.module import new_module


pytestmark = pytest.mark.phase_reproducers


_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts',
                       'stack_drop_gc.ki')


def noop(*args):
    return None


class Collecting:
    collected = 0

    def __del__(self):
        # refill the blocks the dropped argument just vacated, then
        # collect while the drop is still in progress
        keep = [bytearray(b"A" * 15) for _ in range(64)]
        gc.collect()
        Collecting.collected += 1
        del keep


class TestStackDropGC(unittest.IsolatedAsyncioTestCase):

    async def test_finalizer_collecting_during_an_argument_drop(self):
        mod = new_module('stack_drop_gc')
        await run_file_async(mod, _SCRIPT)
        run = vars(mod)['run']

        Collecting.collected = 0
        for _ in range(8):
            self.assertIsNone(await run(noop, Collecting))
        self.assertEqual(8, Collecting.collected)


# The end.
