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
tests.reproducers.test_resume_first_raise

An error raised while a frame waits on a resume reaches that resume
before the frame's extents: the for-each step resume ends the loop on
the iteration's own end exception and declines anything else, after
which a try around the loop catches, a finally runs, a try around a
yield-from catches inside the delegating generator, and a mapped
function's StopIteration ends the loop it feeds.

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
                       'resume_first_raise.ki')


class OneThenBoom:
    """
    A plain iterator: one element, then a ZeroDivisionError from its
    step
    """

    def __init__(self):
        self.count = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.count += 1
        if self.count == 1:
            return 1.0
        return 1 / 0


class TestResumeFirstRaise(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module('resume_first_raise')
        await run_file_async(mod, _SCRIPT)
        return vars(mod)

    async def test_try_around_the_loop_catches(self):
        ns = await self.load()
        self.assertEqual([0, 1, "two"], await ns['caught_around_loop'](5))

    async def test_finally_around_the_loop_runs(self):
        ns = await self.load()
        log = []
        with self.assertRaises(ValueError):
            await ns['finally_around_loop'](5, log)
        self.assertEqual([0, 1, "finally"], log)

    async def test_mapped_function_stop_iteration_ends_the_loop(self):
        ns = await self.load()
        self.assertEqual([0, 10, 20], await ns['truncated_map'](6))

    async def test_try_around_yield_from_catches(self):
        ns = await self.load()
        self.assertEqual([0, 1, "two", "after"],
                         await ns['caught_in_delegation'](5))

    async def test_inner_catch_outer_continues(self):
        ns = await self.load()
        self.assertEqual(
            [(0, 0), (0, 1), (0, "two"), (1, 0), (1, 1), (1, "two")],
            await ns['outer_over_inner'](5))

    async def test_plain_iterator_error_reaches_the_try(self):
        ns = await self.load()
        self.assertEqual([1.0, "zero"],
                         await ns['plain_iter_error'](OneThenBoom(), []))


# The end.
