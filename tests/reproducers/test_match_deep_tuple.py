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
tests.reproducers.test_match_deep_tuple

A capture from a tuple, list or single-value subject is the user's
own value: match does not walk into it, so a tuple nested deeper
than the C stack would carry is captured as is, and a Datum the user
placed inside keeps its own binding.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.cli.host import run_file_async
from koskari.lib import Datum
from koskari.module import new_module
from tests import needs_native_stack


pytestmark = pytest.mark.phase_reproducers


_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts',
                       'match_deep_tuple.ki')


class TestMatchDeepTuple(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module('match_deep_tuple')
        await run_file_async(mod, _SCRIPT)
        return vars(mod)

    @needs_native_stack
    async def test_deeply_nested_tuple_subject(self):
        ns = await self.load()
        stack = await ns['build'](400000)
        self.assertEqual(399999, await ns['peek'](stack))

    async def test_user_datum_inside_a_capture_keeps_its_binding(self):
        ns = await self.load()
        d = Datum(5, None, "mine")
        got = await ns['capture_pair']((1, (d,)))
        self.assertIs(d, got[0])
        self.assertEqual("mine", d.binding)


# The end.
