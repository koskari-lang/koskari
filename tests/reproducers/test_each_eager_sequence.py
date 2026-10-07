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
tests.reproducers.test_each_eager_sequence

The sequence of an each is evaluated when the form runs, in the scope
the form is in, as the outermost iterable of a Python generator
expression is: a later rebind of its name does not change the result,
and a class-body name is in reach.

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
                       'each_eager_sequence.ki')


class TestEachEagerSequence(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module('each_eager_sequence')
        await run_file_async(mod, _SCRIPT)
        return vars(mod)

    async def test_rebind_after_creation_does_not_change_the_result(self):
        mod = await self.load()
        self.assertEqual([10, 20, 30], mod['after_rebind'])

    async def test_dict_each_over_a_class_body_name(self):
        mod = await self.load()
        self.assertEqual({"a": 1, "b": 1}, mod['K'].table)


# The end.
