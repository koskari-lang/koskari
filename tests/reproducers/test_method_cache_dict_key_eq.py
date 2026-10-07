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
tests.reproducers.test_method_cache_dict_key_eq

Resolving a method probes the instance dict, and a non-str key there
with the method name's hash runs its __eq__. The resolver holds the
class attribute it found across that probe, so an __eq__ that drops
the attribute from the class leaves the call with the method it
resolved, and an __eq__ that raises has its exception propagated.

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
                       'method_cache_dict_key_eq.ki')


def make_method():
    return eval("lambda self: 'method result'")


class TestMethodCacheDictKeyEq(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module('method_cache_dict_key_eq')
        await run_file_async(mod, _SCRIPT)
        return vars(mod)['call_m']

    async def test_key_eq_dropping_the_class_attribute(self):
        call_m = await self.load()

        class C:
            pass

        class Evil:
            def __hash__(self):
                return hash("m")

            def __eq__(self, other):
                if "m" in C.__dict__:
                    del C.m
                    Evil.junk = [eval("lambda self: 'HIJACKED'")
                                 for _ in range(200)]
                return False

        C.m = make_method()
        c = C()
        c.__dict__[Evil()] = 1
        self.assertEqual('method result', await call_m(c))

    async def test_key_eq_raising_propagates(self):
        call_m = await self.load()

        class C:
            def m(self):
                return 'method result'

        class Raising:
            def __hash__(self):
                return hash("m")

            def __eq__(self, other):
                raise RuntimeError("probe")

        c = C()
        c.__dict__[Raising()] = 1
        with self.assertRaises(RuntimeError):
            await call_m(c)
        self.assertEqual('method result', await call_m(C()))


# The end.
