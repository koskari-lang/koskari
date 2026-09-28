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
tests.lib.test_match_root_user_code_once

A match arm's type test runs user code once per arm: a metaclass
__instancecheck__ and a subject's __class__ property are each consulted
one time, whether the arm matches or not.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
define typed [v]:
    match v:
        case m :: Fake:
            "hit"
        else:
            "miss"

define shaped [v]:
    match v:
        case Fake[x: a]:
            a
        else:
            "miss"
"""


class TestMatchRootUserCodeOnce(unittest.IsolatedAsyncioTestCase):

    async def load(self, fake):
        module = new_module("match_root_user_code_once")
        vars(module)["__file__"] = "<match_root_user_code_once>"
        vars(module)["Fake"] = fake
        await load_module_source_async(module, SRC)
        return vars(module)

    async def test_metaclass_instancecheck_runs_once_per_arm(self):
        calls = []

        class Meta(type):
            def __instancecheck__(cls, obj):
                calls.append(obj)
                return obj == "yes"

        class Fake(metaclass=Meta):
            pass

        mod = await self.load(Fake)
        self.assertEqual(await mod["typed"]("yes"), "hit")
        self.assertEqual(calls, ["yes"])
        self.assertEqual(await mod["typed"]("no"), "miss")
        self.assertEqual(calls, ["yes", "no"])

    async def test_class_property_read_once_per_arm(self):
        class Fake:
            def __init__(self, x):
                self.x = x

        reads = []

        class Liar:
            x = 7

            @property
            def __class__(self):
                reads.append(1)
                return Fake

        mod = await self.load(Fake)
        self.assertEqual(await mod["shaped"](Liar()), 7)
        self.assertEqual(len(reads), 1)


# The end.
