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
tests.reproducers.test_transform_reentry

A source stepped from Koskari can step the pairwise or zip-longest
consuming it; the inner step runs through the transform's own suite,
and the outer step completes with the elements it holds itself.

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
                       'transform_reentry.ki')


class TestTransformReentry(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module('transform_reentry')
        await run_file_async(mod, _SCRIPT)
        return vars(mod)

    async def test_pairwise_source_exhausting_it_from_inside(self):
        ns = await self.load()
        self.assertEqual(((1, "b"), "stop"), await ns['pair_case']())

    async def test_zip_longest_source_stepping_it_from_inside(self):
        ns = await self.load()
        self.assertEqual(((2, 20), (2, 10)), await ns["zip_case"]())


# The end.
