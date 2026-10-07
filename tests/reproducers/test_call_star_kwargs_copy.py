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
tests.reproducers.test_call_star_kwargs_copy

A **: operand is copied into a fresh dict before the call, as Python's
f(**d) is, so the keys and values stay held for the call however the
caller's dict is mutated while a keyword is bound by name. Keys of any
type pass through, as the dict literal's splat is this call.

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
                       'call_star_kwargs_copy.ki')


class ClearingKey(str):

    def __new__(cls, s, d):
        self = super().__new__(cls, s)
        self.d = d
        return self

    def __eq__(self, other):
        self.d.clear()
        return str.__eq__(self, other)

    def __hash__(self):
        return str.__hash__(self)


class Payload:

    def __init__(self):
        self.payload = "alive"


class TestCallStarKwargsCopy(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module('call_star_kwargs_copy')
        await run_file_async(mod, _SCRIPT)
        return vars(mod)['spread']

    async def test_key_eq_clearing_the_source_dict(self):
        spread = await self.load()
        d = {}
        d[ClearingKey("z", d)] = Payload()
        out = await spread(d)
        junk = [Payload() for _ in range(500)]
        del junk
        self.assertEqual(["z"], list(out))
        self.assertEqual("alive", out["z"].payload)

    async def test_non_str_keys_pass_through(self):
        spread = await self.load()
        self.assertEqual({1: 2}, await spread({1: 2}))

    async def test_dict_subclass_operand_spreads(self):
        spread = await self.load()

        class D(dict):
            pass

        self.assertEqual({"z": 3}, await spread(D(z=3)))


# The end.
