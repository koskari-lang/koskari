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
tests.reproducers.test_foreach_step_resume

The for-each step names a resume, and the element reaches that resume
on every route: in hand from a plain iterator, by a Koskari generator's
yield, through a yield-from delegation two deep, from a generator whose
own body loops over a generator, from a generator that awaits between
yields, and from a Python async iterator whose step suspends.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import os
import unittest

import pytest

from koskari.cli.host import run_file_async
from koskari.module import new_module


pytestmark = pytest.mark.phase_reproducers


_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts',
                       'foreach_step_resume.ki')


async def py_async_source(n):
    for i in range(n):
        await asyncio.sleep(0)
        yield i


class TestForeachStepResume(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module('foreach_step_resume')
        await run_file_async(mod, _SCRIPT)
        return vars(mod)

    async def test_plain_iterator_in_hand(self):
        ns = await self.load()
        self.assertEqual([0, 1, 2, 3], await ns['collect_plain'](4))

    async def test_generator_yield_reaches_the_resume(self):
        ns = await self.load()
        self.assertEqual([0, 1, 2, 3], await ns['collect_gen'](4))

    async def test_delegation_two_deep(self):
        ns = await self.load()
        self.assertEqual([0, 1, 2, 0, 1, 2], await ns['collect_delegated'](3))

    async def test_break_mid_delegation_then_outer_continues(self):
        ns = await self.load()
        self.assertEqual([(0, 0), (1, 0), (2, 0)],
                         await ns['collect_break'](2))

    async def test_generator_whose_body_loops(self):
        ns = await self.load()
        self.assertEqual([0, 10, 20], await ns['collect_nested'](3))

    async def test_generator_yields_after_looping_a_generator_to_its_end(self):
        ns = await self.load()
        self.assertEqual([3], [v async for v in ns['sum_then_yield'](3)])

    async def test_generator_awaiting_between_yields(self):
        ns = await self.load()
        self.assertEqual([0, 1, 2], await ns['collect_awaiting'](3))

    async def test_python_async_iterator_step_suspends(self):
        ns = await self.load()
        self.assertEqual([0, 1, 2],
                         await ns['collect_source'](py_async_source(3)))

    async def test_end_lands_on_the_else_clause(self):
        ns = await self.load()
        self.assertEqual([0, 1, "end"], await ns['collect_else'](2))

    async def test_module_level_loop_stores_by_name(self):
        ns = await self.load()
        self.assertEqual([0, 1, 2], ns['module_out'])

    async def test_continue_under_finally_over_a_generator(self):
        ns = await self.load()
        log = []
        self.assertEqual([2, 3, 4],
                         await ns['continue_under_finally'](5, log))
        self.assertEqual([0, 1, 2, 3, 4], log)

    async def test_continue_in_nested_loops_over_generators(self):
        ns = await self.load()
        self.assertEqual(
            [(0, 1), (0, 2), (1, 0), (1, 2), 1, (2, 0), (2, 1), 2],
            await ns['continue_nested'](3))


# The end.
