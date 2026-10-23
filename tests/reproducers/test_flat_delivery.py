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
tests.reproducers.test_flat_delivery

A flattened producer's element reaches its consumer by tail call, so
the C stack stays flat however many elements pass. Each consumer runs
two hundred thousand elements on a thread with a 512 KiB stack: a
delivery that nested a C frame per element would overflow it.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import os
import threading
import unittest

import pytest

from koskari.cli.host import run_file_async
from koskari.module import new_module


pytestmark = pytest.mark.phase_reproducers


_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts',
                       'flat_delivery.ki')

ELEMENTS = 200_000
STACK = 512 * 1024


def run_on_small_stack(fn, n):
    """
    Await fn(n) on its own event loop in a thread with a small stack,
    and hand back its result or raise what it raised
    """

    box = {}

    async def go():
        return await fn(n)

    def target():
        try:
            box['value'] = asyncio.run(go())
        except BaseException as exc:
            box['error'] = exc

    old = threading.stack_size(STACK)
    try:
        thread = threading.Thread(target=target)
        thread.start()
        thread.join()
    finally:
        threading.stack_size(old)

    if 'error' in box:
        raise box['error']
    return box['value']


class TestFlatDelivery(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        mod = new_module('flat_delivery')
        asyncio.run(run_file_async(mod, _SCRIPT))
        cls.ns = vars(mod)

    def test_foreach_over_a_generator(self):
        self.assertEqual(
            ELEMENTS, run_on_small_stack(self.ns['count_foreach'], ELEMENTS))

    def test_next_over_a_generator(self):
        self.assertEqual(
            ELEMENTS * (ELEMENTS - 1) // 2,
            run_on_small_stack(self.ns['sum_next'], ELEMENTS))

    def test_foreach_over_a_delegation(self):
        self.assertEqual(
            ELEMENTS,
            run_on_small_stack(self.ns['count_delegated'], ELEMENTS))

    def test_foreach_over_a_mapped_function(self):
        self.assertEqual(
            ELEMENTS * (ELEMENTS + 1) // 2,
            run_on_small_stack(self.ns['sum_mapped'], ELEMENTS))


# The end.
