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
tests.reproducers.test_next_resume

The next request names a resume with the instruction as its state and
no node on the chain: the element reaches that resume in hand, by a
Koskari generator's yield, from a transform over a generator, inside a
for-each body over another generator, and the end reaches it as
StopIteration, which the two-argument form answers with its default
and the one-argument form lets stand.

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
                       'next_resume.ki')


class TestNextResume(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module('next_resume')
        await run_file_async(mod, _SCRIPT)
        return vars(mod)

    async def test_two_steps_of_a_generator(self):
        ns = await self.load()
        self.assertEqual((0, 1), await ns['take_two'](5))

    async def test_next_inside_a_for_each_body(self):
        ns = await self.load()
        self.assertEqual([(0, 0), (1, 1), (2, 2)],
                         await ns['next_in_loop'](3))

    async def test_next_over_a_transform_of_a_generator(self):
        ns = await self.load()
        self.assertEqual((0, 10, None), await ns['next_over_map'](2))

    async def test_default_at_the_end(self):
        ns = await self.load()
        self.assertEqual("done", await ns['default_after_end'](3))

    async def test_stop_iteration_at_the_end(self):
        ns = await self.load()
        self.assertEqual("stopped", await ns['stop_after_end'](3))

    async def test_producer_error_reaches_the_try(self):
        ns = await self.load()
        self.assertEqual((1, "boom"), await ns['error_in_producer']())

    async def test_fused_forms_by_name(self):
        ns = await self.load()
        self.assertEqual((0, 1, 99), await ns['next_by_name'](2))

    async def test_producer_error_beats_the_default(self):
        ns = await self.load()
        self.assertEqual((1, "boom"), await ns['error_with_default']())


# The end.
